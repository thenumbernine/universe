#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <direct.h>
#include <math.h>
#include <string.h>
#include <pthread.h>

#include <exception>
#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <fstream>

#include "exception.h"
#include "stat.h"
#include "util.h"
#include "batch.h"

#define numberof(x)	(sizeof(x)/sizeof(*(x)))
#define endof(x)	((x)+numberof(x))

using namespace std;

int FORCE = 0;

class StatBatchProcessor;

struct StatWorker {
	StatBatchProcessor &batch;
	int numOutliers;
	
	//for the batch processor
	typedef string ArgType;
	string desc(const ArgType &basename);
	
	StatWorker(BatchProcessor<StatWorker> *batch_); 
	~StatWorker();

	void operator()(const ArgType &basename);
};

class StatBatchProcessor : public BatchProcessor<StatWorker> {
	bool useTotalStats;
	int numOutliers;
	StatSet totalStats;
	Mutex outlierMutex;
	friend class StatWorker;
public:
	string datasetname;
	
	StatBatchProcessor();
	void setTotalStats(const StatSet &totalStats_);
	void done();
};


StatWorker::StatWorker(BatchProcessor<StatWorker> *batch_) 
: 	batch(*(StatBatchProcessor*)batch_),
	numOutliers(0)
{}

StatWorker::~StatWorker() {
	CritSec outlierCS(batch.outlierMutex);
	batch.numOutliers += numOutliers;
}

string StatWorker::desc(const ArgType &basename) { 
	return string() + "file " + basename; 
}

void StatWorker::operator()(const ArgType &basename) {

	string ptfilename = string("datasets/") + batch.datasetname + "/points/" + basename + ".f32";
	string statsFilename = string("datasets/") + batch.datasetname + "/stats/" + basename + ".stats";
	
	if (!FORCE && fileexists(statsFilename)) {
		throw Exception() << "file " << statsFilename << " already exists";
	}

	StatSet stats;
	
	//TODO mmap http://www.cs.purdue.edu/homes/fahmy/cs503/mmap.txt
	streamsize vtxbufsize = 0;
	float *vtxbuf = (float*)getFile(ptfilename, &vtxbufsize);
	float *vtxbufend = vtxbuf + (vtxbufsize / sizeof(float));
	for (float *vtx = vtxbuf; vtx < vtxbufend; vtx += 3) { 
		double values[NUM_STATSET_VARS];
	
		if (batch.useTotalStats) {
			//filter x y z
			if ((vtx[0] < batch.totalStats.x.avg - 3 * batch.totalStats.x.stddev) ||
				(vtx[0] > batch.totalStats.x.avg + 3 * batch.totalStats.x.stddev) ||
				(vtx[1] < batch.totalStats.y.avg - 3 * batch.totalStats.y.stddev) ||
				(vtx[1] > batch.totalStats.y.avg + 3 * batch.totalStats.y.stddev) ||
				(vtx[2] < batch.totalStats.z.avg - 3 * batch.totalStats.z.stddev) ||
				(vtx[2] > batch.totalStats.z.avg + 3 * batch.totalStats.z.stddev))
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
	CritSec runningCS(runningMutex, true);
	if (runningCS.fail()) throw Exception() << "can't modify while running";
	useTotalStats = true;
	totalStats = totalStats_;
}

void StatBatchProcessor::done() {
	if (useTotalStats) cout << "removed " << numOutliers << " outliers" << endl;
}

void showhelp() {
	cout
	<< "usage: getstats <options>" << endl
	<< "options:" << endl
	<< "	--set <set>			specify the dataset. default is 'allsky'." << endl
	<< "	--file <file>		convert only this file.  omit path and ext." << endl
	<< "	--all				convert all files in the <set>/points dir." << endl
	<< "	--force				generate the stats file even if it already exists." << endl
	<< "	--threads <n>		specify the number of threads to use." << endl
	<< "	--remove-outliers	use the stats/total.stats file to remove outliers." << endl
	;
}

int main(int argc, char **argv) {
	try {
		int totalFiles = 0;
		bool gotDir = false, gotFile = false, removeOutliers = false;
		StatBatchProcessor batch;
		
		for (int k = 1; k < argc; k++) {
			if (!strcmp(argv[k], "--force")) {
				FORCE = 1;
			} else if (!strcmp(argv[k], "--set") && k < argc-1) {
				batch.datasetname = argv[++k];
			} else if (!strcmp(argv[k], "--all")) {
				gotDir = true;

			} else if (!strcmp(argv[k], "--file") && k < argc-1) {
				gotFile = true;
				batch.addThreadArg(argv[++k]);
				totalFiles++;
			} else if (!strcmp(argv[k], "--remove-outliers")) {
				removeOutliers = true;
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

		if (removeOutliers) {
			string totalStatFilename = string("datasets/") + batch.datasetname + "/stats/total.stats";
			if (!fileexists(totalStatFilename)) throw Exception() << "expected file to exist: " << totalStatFilename;
			StatSet totalStats;
			totalStats.read(totalStatFilename.c_str());
			totalStats.calcSqAvg();
			batch.setTotalStats(totalStats);	
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

		mkdir((string("datasets/") + batch.datasetname + "/stats").c_str());

		double deltaTime = profile("getstats", batch);
		cout << (deltaTime / (double)totalFiles) << " seconds per file" << endl;

		batch.done();

	} catch (exception &t) {
		cerr << "error: " << t.what() << endl;
		return 1;
	}
	return 0;
}

