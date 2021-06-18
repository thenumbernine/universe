#include <string.h>

#include <list>
#include <string>
#include <fstream>

#include "exception.h"
#include "stat.h"
#include "util.h"

using namespace std;

struct TotalStatWorker {
	list<string> files;
	string datasetname;

	TotalStatWorker(const string &datasetname_) 
	: datasetname(datasetname_)
	{
		list<string> dirFilenames = getDirFileNames(string("datasets/") + datasetname + "/points");
		for (list<string>::iterator i = dirFilenames.begin(); i != dirFilenames.end(); ++i) {
			string base, ext;
			getFileNameParts(*i, base, ext);
			if (ext == "f32") {
				files.push_back(base);
			}
		}
	}
	
	void operator()() {
		StatSet totalStats;

		for (list<string>::iterator i = files.begin(); i != files.end(); ++i) {
			const string &basename = *i;
			string statsfilename = string("datasets/") + datasetname + "/stats/" + basename + ".stats";
			
			StatSet stats;
			stats.read(statsfilename.c_str());
			stats.calcSqAvg();
			totalStats.accum(stats);
		}

		totalStats.calcStdDev();
		totalStats.write(string("datasets/") + datasetname + "/stats/total.stats");
	}
};

void showhelp() {
	cout
	<< "usage: gettotalstats <options>" << endl
	<< "options:" << endl
	<< "	--set <set>	specify the dataset.  default is 'allsky'" << endl
	;
}

int main(int argc, char **argv) {
	try {
		string datasetname = "allsky";
		for (int k = 1; k < argc; k++) {
			if (!strcmp(argv[k], "--help")) {
				showhelp();
				return 0;
			} else if (!strcmp(argv[k], "--set") && k < argc-1) {
				datasetname = argv[++k];
			} else {
				showhelp();
				return 0;
			}
		}

		TotalStatWorker totalWorker(datasetname);
		int totalFiles = totalWorker.files.size();
		double deltaTime = profile("get total", totalWorker);
		cout << totalFiles << " files" << endl;
		cout << (deltaTime / (double)totalFiles) << " seconds per file" << endl;
	//not catching ...
	} catch (Exception &t) {
		cerr << "error: " << t << endl;
		return 1;
	}
	return 0;
}

