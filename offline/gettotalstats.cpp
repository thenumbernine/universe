#include "exception.h"
#include "stat.h"
#include "util.h"

struct TotalStatWorker {
	std::list<std::string> files;
	std::string datasetname;

	TotalStatWorker(const std::string &datasetname_) 
	: datasetname(datasetname_)
	{
		for (auto const & i : getDirFileNames(std::string() + "datasets/" + datasetname + "/points")) {
			std::string base, ext;
			getFileNameParts(i, base, ext);
			if (ext == "f32") {
				files.push_back(base);
			}
		}
	}
	
	void operator()() {
		StatSet totalStats;

		for (auto const & basename : files) {
			std::string const statsfilename = std::string() + "datasets/" + datasetname + "/stats/" + basename + ".stats";
			
			StatSet stats;
			stats.read(statsfilename);
			stats.calcSqAvg();
			totalStats.accum(stats);
		}

		totalStats.calcStdDev();
		totalStats.write(std::string() + "datasets/" + datasetname + "/stats/total.stats");
	}
};

void _main(std::vector<std::string> const & args) {
	std::string datasetname = "allsky";
	HandleArgs(args, {
		{"--set", {"<set> = specify the dataset.  default is 'allsky'.", {[&](std::string s){
			datasetname = s;
		}}}},
	});

	TotalStatWorker totalWorker(datasetname);
	int totalFiles = totalWorker.files.size();
	double deltaTime = profile("get total", [&]() {
		totalWorker();
	});
	std::cout << totalFiles << " files" << std::endl;
	std::cout << (deltaTime / (double)totalFiles) << " seconds per file" << std::endl;
}

int main(int argc, char **argv) {
	try {
		_main({argv, argv + argc});
	} catch (std::exception &t) {
		std::cerr << "error: " << t.what() << std::endl;
		return 1;
	}
	return 0;
}
