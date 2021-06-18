#include <cassert>
#include <cmath>
#include <iostream>
#include <fstream>
#include "exception.h"
#include "stat.h"
#include "util.h"
#include "batch.h"

int INTERACTIVE = 0;
int VERBOSE = 0;

struct Volume {
	int size;
	float *density;
	
	int usedCount;
	int unusedCount;
	
	Volume(int size_)
	:	size(size_),
		density(new float[size_ * size_ * size_]),
		usedCount(0),
		unusedCount(0)
	{
		//init data
		memset(density, 0, sizeof(float) * size * size * size);
	}

	virtual ~Volume() {
		delete[] density;
	}

	void applyFile(
		const char *filename,
		const float *center, 
		const float *bmin, 
		const float *bmax
	) {
		std::streamsize vtxbufsize = 0;
		float *vtxbuf = (float*)getFile(filename, &vtxbufsize);
		float *vtxbufend = vtxbuf + (vtxbufsize / sizeof(float));
		int ivtx[3];
		for (float *vtx = vtxbuf; vtx < vtxbufend; vtx += 3) { 
			for (int i = 0; i < 3; i++) {
				ivtx[i] = (double)size * (vtx[i] - bmin[i]) / (bmax[i] - bmin[i]);
			}
			if (ivtx[0] < 0 || ivtx[0] >= size ||
				ivtx[1] < 0 || ivtx[1] >= size ||
				ivtx[2] < 0 || ivtx[2] >= size)
			{
				unusedCount++;
				continue;
			}
			usedCount++;
			
			density[ivtx[0] + size * (ivtx[1] + size * ivtx[2])]++;
		}
		delete[] vtxbuf;
	}

	void accumulate(const Volume &v) {
		assert(size == v.size);
		for (int i = 0; i < size * size * size; i++) {
			density[i] += v.density[i];
		}
		unusedCount += v.unusedCount;
		usedCount += v.usedCount;
	}

	void write(const std::string &outfilename) {
		float maxDensity = 0;
		for (int i = 0; i < size * size * size; i++) {
			float &densityRef = density[i];
			if (densityRef > maxDensity) maxDensity = densityRef;
		}
		
		std::cout << "max cell density " << maxDensity << std::endl;
		assert(maxDensity > 0);
		//...and normalize ... and write out ...
		float invMaxDensity = 1. / maxDensity;
		long int numNonzeroCells = 0;
		for (int i = 0; i < size * size * size; i++) {
			density[i] *= invMaxDensity;	
			if (density[i]) numNonzeroCells++;
		}
		std::cout << usedCount << " points used" << std::endl;
		std::cout << unusedCount << " points are out of bounds" << std::endl;
		std::cout << (100. * (double)numNonzeroCells / ((double)size * size * size)) << "% of volume cells are nonzero" << std::endl;

		std::ofstream f(outfilename.c_str(), std::ios::out | std::ios::binary);
		if (!f.is_open()) throw Exception() << "failed to open " << outfilename << " for writing";
		f.write(reinterpret_cast<char *>(density), sizeof(float) * size * size * size);
	}
};

struct VolumeBatchProcessor;

struct VolumeWorker {
	VolumeBatchProcessor &batch;
	Volume volume;
	typedef std::string ArgType;
	std::string desc(const ArgType &basename);

	VolumeWorker(BatchProcessor<VolumeWorker> *batch_);
	~VolumeWorker();

	void operator()(const ArgType &basename);
};

struct VolumeBatchProcessor : public BatchProcessor<VolumeWorker> {
	StatSet totalStats;
	float center[3], bmin[3], bmax[3];
	Volume volume;
	std::mutex volumeMutex;
	std::string datasetname;

	VolumeBatchProcessor();
	void init();
	void done();
};

VolumeWorker::VolumeWorker(BatchProcessor<VolumeWorker> *batch_)
:	batch(*(VolumeBatchProcessor*)batch_),
	volume(batch.volume.size)
{}

VolumeWorker::~VolumeWorker() {
	std::unique_lock<std::mutex> volumeCS(batch.volumeMutex);
	batch.volume.accumulate(volume);
}

std::string VolumeWorker::desc(const ArgType &basename) {
	return std::string() + "file " + basename;
}

void VolumeWorker::operator()(const ArgType &basename) {
	std::string ptfilename = std::string("datasets/") + batch.datasetname + "/points/" + basename + ".f32";
	volume.applyFile(
		ptfilename.c_str(), 
		batch.center, 
		batch.bmin, 
		batch.bmax);
}

VolumeBatchProcessor::VolumeBatchProcessor()
: 	BatchProcessor<VolumeWorker>(),
	volume(256),
	datasetname("allsky")
{
}

void VolumeBatchProcessor::init() {
	totalStats.read((std::string() + "datasets/" + datasetname + "/stats/total.stats").c_str());

	float halfWidth[3];
	for (int i = 0; i < 3; i++) {
		halfWidth[i] = .5 * (totalStats.vars()[i].max - totalStats.vars()[i].min);
		center[i] = totalStats.vars()[i].min + halfWidth[i]; 
	}
	float maxHalfWidth = fabs(halfWidth[0]);
	for (int i = 1; i < 3; i++) {
		float absHalfWidth = fabs(halfWidth[i]);
		if (absHalfWidth > maxHalfWidth) maxHalfWidth = absHalfWidth;
	}
	maxHalfWidth *= (float)(volume.size+1) / (float)volume.size;
	for (int i = 0; i < 3; i++) {
		bmin[i] = center[i] - maxHalfWidth;
		bmax[i] = center[i] + maxHalfWidth;
	}

	std::cout << "center " << center[0] << ", " << center[1] << ", " << center[2] << std::endl;
	std::cout << "min " << bmin[0] << ", " << bmin[1] << ", " << bmin[2] << std::endl;
	std::cout << "max " << bmax[0] << ", " << bmax[1] << ", " << bmax[2] << std::endl;
}

void VolumeBatchProcessor::done() {
	volume.write(std::string() + "datasets/" + datasetname + "/density.vol");
}

void showhelp() {
	std::cout
	<< "usage: genvolume <options>" << std::endl
	<< "options:" << std::endl
	<< "    --set <set>      specify the dataset.  default is 'allsky'" << std::endl
	<< "    --all            convert all files in the allsky-gz dir." << std::endl
	<< "    --file <file>    convert only this file.  omit path and ext." << std::endl
	<< "    --threads <n>    specify the number of threads to use." << std::endl
	<< "    --verbose        shows verbose information." << std::endl
	<< "    --wait           waits for key at each entry.  implies verbose." << std::endl
	;
}

void _main(std::vector<std::string> const & args) {
	bool gotDir = false, gotFile = false;
	VolumeBatchProcessor batch;
	int totalFiles = 0;
	
	for (int k = 1; k < args.size(); k++) {
		if (args[k] == "--set" && k < args.size()-1) {
			batch.datasetname = args[++k];
		} else if (args[k] == "--verbose") {
			VERBOSE = 1;
		} else if (args[k] == "--wait") {
			VERBOSE = 1;
			INTERACTIVE = 1;
		} else if (args[k] == "--all") {
			gotDir = true;
		} else if (args[k] == "--file" && k < args.size()-1) {
			gotFile = true;
			batch.addThreadArg(args[++k]);
			totalFiles++;
		} else if (args[k] == "--threads" && k < args.size()-1) {
			batch.setNumThreads(atoi(args[++k].c_str()));
		} else {
			showhelp();
			return;
		}
	}
	
	if (!gotDir && !gotFile) {
		showhelp();
		return;
	}

	if (gotDir) {
		for (auto const & i : getDirFileNames(std::string() + "datasets/" + batch.datasetname + "/points")) {
			std::string base, ext;
			getFileNameParts(i, base, ext);
			if (ext == "f32") {
				batch.addThreadArg(base);
				totalFiles++;
			}	
		}
	}

	batch.init();

	double deltaTime = profile("genvolume", [&]() {
		batch(); 
	});
	std::cout << (deltaTime / (double)totalFiles) << " seconds per file" << std::endl;

	batch.done();
}

int main(int argc, char **argv) {
	try {
		_main(std::vector<std::string>(argv, argv + argc));
	} catch (std::exception &t) {
		std::cerr << "error: " << t.what() << std::endl;
		return 1;
	}
	return 0;
}
