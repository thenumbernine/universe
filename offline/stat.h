#ifndef STAT_H
#define STAT_H

#include <string>
#include <ostream>
#include <math.h>

enum {
	STAT_MIN,
	STAT_MAX,
	STAT_AVG,
	STAT_SQAVG,
	STAT_STDDEV,
	NUM_STAT_VARS
};

struct Stat {
	double min, max, avg, sqavg, stddev;
	static const char *varnames[NUM_STAT_VARS];
	Stat() : min(INFINITY), max(-INFINITY), avg(0), sqavg(0), stddev(0) {}
	double *vars() { return &min; }
	const double *vars() const { return &min; }

	void calcSqAvg();
	void calcStdDev();
	void accum(double value, double newCount);
	
	struct RW {
		const Stat &stat;
		const std::string &name;
		RW(const Stat &stat_, const std::string &name_) 
		: stat(stat_), name(name_) {}
	};

	RW rw(const std::string &name) { return RW(*this, name); }
};

std::ostream &operator<<(std::ostream &o, const Stat::RW &statwrite);

enum {
	STATSET_X,
	STATSET_Y,
	STATSET_Z,
	STATSET_R,
	STATSET_PHI,
	STATSET_THETA,
	NUM_STATSET_VARS
};

struct StatSet {
	Stat x, y, z, r, phi, theta;
	double count;
	static const char *varnames[NUM_STATSET_VARS];
	StatSet() : count(0) {}
	Stat *vars() { return &x; }
	const Stat *vars() const { return &x; }
	void read(const char *filename);
	void calcSqAvg();
	void calcStdDev();
	void accum(const double *value);
	void accum(const StatSet &set);
	void write(const std::string &dstfilename);
};


#endif

