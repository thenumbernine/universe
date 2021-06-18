#include <cmath>
#include <exception>
#include <iostream>
#include <string>
#include <list>
#include <fstream>
#include <filesystem>
#include "exception.h"
#include "stat.h"
#include "util.h"
#include "batch.h"

int FORCE = 0;

struct StatBatchProcessor;

struct StatWorker {
	StatBatchProcessor* batch = nullptr;
	int numOutliers;
	
	//for the batch processor
	typedef std::string ArgType;
	std::string desc(const ArgType &basename);
	
	StatWorker(BatchProcessor<StatWorker> *batch_); 
	~StatWorker();

	void operator()(const ArgType &basename);
};

struct StatBatchProcessor : public BatchProcessor<StatWorker> {
protected:
	bool useTotalStats;
	int numOutliers;
	StatSet totalStats;
	std::mutex outlierMutex;
	friend struct StatWorker;
public:
	std::string datasetname;
	
	StatBatchProcessor();
	void setTotalStats(const StatSet &totalStats_);
	void done();
};


StatWorker::StatWorker(BatchProcessor<StatWorker> *batch_) 
: 	batch(static_cast<StatBatchProcessor*>(batch_)),
	numOutliers(0)
{}

StatWorker::~StatWorker() {
	std::unique_lock<std::mutex> outlierCS(batch->outlierMutex);
	batch->numOutliers += numOutliers;
}

std::string StatWorker::desc(const ArgType &basename) { 
	return std::string() + "file " + basename; 
}

void StatWorker::operator()(const ArgType &basename) {
	std::string ptfilename = std::string() + "datasets/" + batch->datasetname + "/points/" + basename + ".f32";
	std::string statsFilename = std::string() + "datasets/" + batch->datasetname + "/stats/" + basename + ".stats";
	
	if (!FORCE && std::filesystem::exists(statsFilename)) {
		throw Exception() << "file " << statsFilename << " already exists";
	}

	StatSet stats;
	
	//TODO mmap http://www.cs.purdue.edu/homes/fahmy/cs503/mmap.txt
	std::streamsize vtxbufsize = 0;
	float const * const vtxbuf = (float*)getFile(ptfilename, &vtxbufsize);
	float const * const vtxbufend = vtxbuf + (vtxbufsize / sizeof(float));
	for (float const * vtx = vtxbuf; vtx < vtxbufend; vtx += 3) { 
		double values[NUM_STATSET_VARS];
	
		if (batch->useTotalStats) {
			//filter x y z
			if ((vtx[0] < batch->totalStats.x.avg - 3 * batch->totalStats.x.stddev) ||
				(vtx[0] > batch->totalStats.x.avg + 3 * batch->totalStats.x.stddev) ||
				(vtx[1] < batch->totalStats.y.avg - 3 * batch->totalStats.y.stddev) ||
				(vtx[1] > batch->totalStats.y.avg + 3 * batch->totalStats.y.stddev) ||
				(vtx[2] < batch->totalStats.z.avg - 3 * batch->totalStats.z.stddev) ||
				(vtx[2] > batch->totalStats.z.avg + 3 * batch->totalStats.z.stddev))
			{
				numOutliers++;
				continue;
			}
		}

		values[STATSET_X] = vtx[0];
		values[STATSET_Y] = vtx[1];
		values[STATSET_Z] = vtx[2];
		values[STATSET_R] = sqrt(values[STATSET_X]*values[STATSET_X] + values[STATSET_Y]*values[STATSET_Y] + values[STATSET_Z]*values[STATSET_Z]);
		values[STATSET_PHI] = atan2(values[STATSET_Y], values[STATSET_X]);
		values[STATSET_THETA] = acos(values[STATSET_Z] / values[STATSET_R]);

		stats.accum(values);
	}
	delete[] vtxbuf;

	stats.calcStdDev();
	stats.write(statsFilename);
}

StatBatchProcessor::StatBatchProcessor()
:	useTotalStats(false),
	numOutliers(0),
	BatchProcessor<StatWorker>(),
	datasetname("allsky")
{}
	
void StatBatchProcessor::setTotalStats(const StatSet &totalStats_) {
	std::unique_lock<std::mutex> runningCS(runningMutex, std::try_to_lock);
	if (!runningCS) throw Exception() << "can't modify while running";
	useTotalStats = true;
	totalStats = totalStats_;
}

void StatBatchProcessor::done() {
	if (useTotalStats) std::cout << "removed " << numOutliers << " outliers" << std::endl;
}

void showhelp() {
	std::cout
	<< "usage: getstats <options>" << std::endl
	<< "options:" << std::endl
	<< "    --set <set>          specify the dataset. default is 'allsky'." << std::endl
	<< "    --file <file>        convert only this file.  omit path and ext." << std::endl
	<< "    --all                convert all files in the <set>/points dir." << std::endl
	<< "    --force              generate the stats file even if it already exists." << std::endl
	<< "    --threads <n>        specify the number of threads to use." << std::endl
	<< "    --remove-outliers    use the stats/total.stats file to remove outliers." << std::endl
	;
}

void _main(std::vector<std::string> const & args) {
	int totalFiles = 0;
	bool gotDir = false, gotFile = false, removeOutliers = false;
	StatBatchProcessor batch;
	
	for (int k = 1; k < args.size(); k++) {
		if (args[k] == "--force") {
			FORCE = 1;
		} else if (args[k] == "--set" && k < args.size()-1) {
			batch.datasetname = args[++k];
		} else if (args[k] == "--all") {
			gotDir = true;

		} else if (args[k] == "--file" && k < args.size()-1) {
			gotFile = true;
			batch.addThreadArg(args[++k]);
			totalFiles++;
		} else if (args[k] == "--remove-outliers") {
			removeOutliers = true;
		} else if (args[k] == "--threads" && k < args.size()-1) {
			batch.setNumThreads(atoi(args[++k].c_str()));
		} else {
			std::cout << "unknown command: " << args[k] << std::endl;
			showhelp();
			return;
		}
	}

	if (!gotDir && !gotFile) {
		std::cout << "expected a file or a dir" << std::endl;
		showhelp();
		return;
	}

	if (removeOutliers) {
		std::string totalStatFilename = std::string("datasets/") + batch.datasetname + "/stats/total.stats";
		if (!std::filesystem::exists(totalStatFilename)) throw Exception() << "expected file to exist: " << totalStatFilename;
		StatSet totalStats;
		totalStats.read(totalStatFilename.c_str());
		totalStats.calcSqAvg();
		batch.setTotalStats(totalStats);	
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

	std::filesystem::create_directory(std::string() + "datasets/" + batch.datasetname + "/stats");

	double deltaTime = profile("getstats", [&](){
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
