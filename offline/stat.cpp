#include <fstream>
#include <map>
#include <cassert>

#include "stat.h"
#include "exception.h"

using namespace std;

const char *Stat::varnames[NUM_STAT_VARS] = {
	"min", "max", "avg", "sqavg", "stddev"
};

void Stat::calcSqAvg() {
	sqavg = stddev * stddev + avg * avg;
}

void Stat::calcStdDev() {
	stddev = sqrt(sqavg - avg * avg);	
}

void Stat::accum(double v, double n) {
	if (v < min) min = v;
	if (v > max) max = v;
	avg += (v - avg) / n;
	sqavg += (v*v - sqavg) / n;
}

ostream &operator<<(ostream &o, const Stat::RW &statwrite) {
	for (int j = 0; j < NUM_STAT_VARS; j++) {
		if (j == STAT_SQAVG) continue;
		o << statwrite.name << "_" << Stat::varnames[j] 
			<< " = " << statwrite.stat.vars()[j] << endl;
	}
	return o;
}

const char *StatSet::varnames[NUM_STATSET_VARS] = {
	"x", "y", "z", "r", "phi", "theta"	
};
	
void StatSet::read(const char *filename) {
	//read all first so we can complain if any fields are missing
	ifstream f(filename);
	if (!f.is_open()) throw Exception() << "failed to open file " << filename;	
	
	map<string, double> m;
	while (!f.eof()) {
		string line;
		getline(f, line);
		if (f.fail()) break;
		stringstream ss(line);
		string key, eq;
		double value;
		ss >> key >> eq >> value;
		assert(eq == "=");
		if (m.find(key) != m.end()) throw Exception() << "found a variable twice " << key << " in file " << filename;
		m[key] = value; 
	}

	map<string, double>::iterator mi;
	mi = m.find("count");
	if (mi == m.end()) throw Exception() << "failed to find count";
	count = mi->second;

	for (int i = 0; i < NUM_STATSET_VARS; i++) {
		vars()[i].sqavg = 0;
		for (int j = 0; j < NUM_STAT_VARS; j++) {
			if (j == STAT_SQAVG) continue;	//not in the files
			string key = string() + StatSet::varnames[i] + "_" + Stat::varnames[j];
			mi = m.find(key);
			if (mi == m.end()) throw Exception() << "failed to find " << key;
			vars()[i].vars()[j] = mi->second;
		}
	}
}

//calcs sqavg based on stddev and avg
void StatSet::calcSqAvg() {
	for (int i = 0; i < NUM_STATSET_VARS; i++) {
		vars()[i].calcSqAvg();
	}
}

//calcs stddeev based on sqavg and avg
void StatSet::calcStdDev() {
	for (int i = 0; i < NUM_STATSET_VARS; i++) {
		vars()[i].calcStdDev();
	}
}

//accumulate a single sample
void StatSet::accum(const double *values) {
	count++;
	for (int i = 0; i < NUM_STATSET_VARS; i++) {
		vars()[i].accum(values[i], (double)count);
	}
}

/*
accumulate an entire StatSet of samples
updates min, max, avg, and sqavg
based on their values in the provided StatSet
*/
void StatSet::accum(const StatSet &set) {
	count += set.count;
	double totalChangeRatio = (double)set.count / (double)count;
	for (int i = 0; i < NUM_STATSET_VARS; i++) {
		if (set.vars()[i].min < vars()[i].min) vars()[i].min = set.vars()[i].min;	
		if (set.vars()[i].max > vars()[i].max) vars()[i].max = set.vars()[i].max;	
		
		vars()[i].avg += (set.vars()[i].avg - vars()[i].avg) * totalChangeRatio;
		vars()[i].sqavg += (set.vars()[i].sqavg - vars()[i].sqavg) * totalChangeRatio;
	}
}

void StatSet::write(const string &dstfilename) {
	ofstream f(dstfilename.c_str());
	f.precision(50);
	f << "count = " << count << endl;
	for (int k = 0; k < NUM_STATSET_VARS; k++) {
		f << vars()[k].rw(string(StatSet::varnames[k]));
	}
	f.close();		
}

