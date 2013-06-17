#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <iostream>
#include <fstream>

#include "exception.h"
#include "stat.h"
#include "util.h"
#include "batch.h"

using namespace std;

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
		streamsize vtxbufsize = 0;
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

	void write(const string &outfilename) {
		float maxDensity = 0;
		for (int i = 0; i < size * size * size; i++) {
			float &densityRef = density[i];
			if (densityRef > maxDensity) maxDensity = densityRef;
		}
		
		cout << "max cell density " << maxDensity << endl;
		assert(maxDensity > 0);
		//...and normalize ... and write out ...
		float invMaxDensity = 1. / maxDensity;
		long int numNonzeroCells = 0;
		for (int i = 0; i < size * size * size; i++) {
			density[i] *= invMaxDensity;	
			if (density[i]) numNonzeroCells++;
		}
		cout << usedCount << " points used" << endl;
		cout << unusedCount << " points are out of bounds" << endl;
		cout << (100. * (double)numNonzeroCells / ((double)size * size * size)) << "% of volume cells are nonzero" << endl;

		ofstream f(outfilename.c_str(), ios::out | ios::binary);
		if (!f.is_open()) throw Exception() << "failed to open " << outfilename << " for writing";
		f.write((char *)density, sizeof(float) * size * size * size);
	}
};

struct VolumeBatchProcessor;

struct VolumeWorker {
	VolumeBatchProcessor &batch;
	Volume volume;
	typedef string ArgType;
	string desc(const ArgType &basename);

	VolumeWorker(BatchProcessor<VolumeWorker> *batch_);
	~VolumeWorker();

	void operator()(const ArgType &basename);
};

struct VolumeBatchProcessor : public BatchProcessor<VolumeWorker> {
	StatSet totalStats;
	float center[3], bmin[3], bmax[3];
	Volume volume;
	Mutex volumeMutex;
	string datasetname;

	VolumeBatchProcessor();
	void init();
	void done();
};

VolumeWorker::VolumeWorker(BatchProcessor<VolumeWorker> *batch_)
:	batch(*(VolumeBatchProcessor*)batch_),
	volume(batch.volume.size)
{}

VolumeWorker::~VolumeWorker() {
	CritSec volumeCS(batch.volumeMutex);
	batch.volume.accumulate(volume);
}

string VolumeWorker::desc(const ArgType &basename) {
	return string() + "file " + basename;
}

void VolumeWorker::operator()(const ArgType &basename) {
	string ptfilename = string("datasets/") + batch.datasetname + "/points/" + basename + ".f32";
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
	totalStats.read((string("datasets/") + datasetname + "/stats/total.stats").c_str());

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

	cout << "center " << center[0] << ", " << center[1] << ", " << center[2] << endl;
	cout << "min " << bmin[0] << ", " << bmin[1] << ", " << bmin[2] << endl;
	cout << "max " << bmax[0] << ", " << bmax[1] << ", " << bmax[2] << endl;
}

void VolumeBatchProcessor::done() {
	volume.write(string("datasets/")  + datasetname + "/density.vol");
}

void showhelp() {
	cout
	<< "usage: genvolume <options>" << endl
	<< "options:" << endl
	<< "	--set <set>	specify the dataset.  default is 'allsky'" << endl
	<< "	--all		convert all files in the allsky-gz dir." << endl
	<< "	--file <file>	convert only this file.  omit path and ext." << endl
	<< "	--threads <n>		specify the number of threads to use." << endl
	<< "	--verbose	shows verbose information." << endl
	<< "	--wait		waits for key at each entry.  implies verbose." << endl
	;
}

int main(int argc, char **argv) {
	try {
		bool gotDir = false, gotFile = false;
		VolumeBatchProcessor batch;
		int totalFiles = 0;
		
		for (int k = 1; k < argc; k++) {
			if (!strcmp(argv[k], "--set") && k < argc-1) {
				batch.datasetname = argv[++k];
			} else if (!strcmp(argv[k], "--verbose")) {
				VERBOSE = 1;
			} else if (!strcmp(argv[k], "--wait")) {
				VERBOSE = 1;
				INTERACTIVE = 1;
			} else if (!strcmp(argv[k], "--all")) {
				gotDir = true;
			} else if (!strcmp(argv[k], "--file") && k < argc-1) {
				gotFile = true;
				batch.addThreadArg(argv[++k]);
				totalFiles++;
			} else if (!strcmp(argv[k], "--threads") && k < argc-1) {
				batch.setNumThreads(atoi(argv[++k]));
			} else {
				showhelp();
				return 0;
			}
		}
		
		if (!gotDir && !gotFile) {
			showhelp();
			return 0;
		}

		if (gotDir) {
			list<string> dirFilenames = getDirFileNames(string("datasets/") + batch.datasetname + "/points");
			for (list<string>::iterator i = dirFilenames.begin(); i != dirFilenames.end(); ++i) {
				string base, ext;
				getFileNameParts(*i, base, ext);
				if (ext == "f32") {
					batch.addThreadArg(base);
					totalFiles++;
				}	
			}
		}

		batch.init();

		double deltaTime = profile("genvolume", batch);
		cout << (deltaTime / (double)totalFiles) << " seconds per file" << endl;

		batch.done();

	} catch (exception &t) {
		cerr << "error: " << t.what() << endl;
		return 1;
	}
	return 0;
}
