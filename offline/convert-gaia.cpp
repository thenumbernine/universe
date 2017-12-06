// THIS WAS COPIED FROM convert-sdss3.cpp ... maybe unify the two for a fits reader / libraries?

/*
usage:
convert-gaia			generates point file
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#include <vector>
#include <map>
#include <string>
#include <sstream>
#include <limits>

#include "fitsio.h"

#include "stat.h"
#include "exception.h"
#include "util.h"
#include "defs.h"

using namespace std;

bool verbose = false;
bool getColumns = false;
bool interactive = false;
bool omitWrite = false;
bool showRanges = false;

//fits is rigit and I am lazy.  use its writer to stderr and return the same status
string fitsGetError(int status) {
	//let the default stderr writer do its thing
	fits_report_error(stderr, status);
	//then capture the 30-char-max error
	char buffer[32];
	bzero(buffer, sizeof(buffer));
	fits_get_errstatus(status, buffer);
	ostringstream ss;
	ss << "FITS error " << status << ": " << buffer;
	return ss.str();
}

#define FITS_SAFE(x) \
{\
	int status = 0;\
	x;\
	if (status) throw Exception() << fitsGetError(status); \
}

template<typename CTYPE> struct FITSType {};
#define FITS_TYPE(CTYPE, FITSTYPE) \
template<> struct FITSType<CTYPE> { enum { type = FITSTYPE }; }

FITS_TYPE(bool, TBYTE);
FITS_TYPE(short, TSHORT);
FITS_TYPE(int, TINT32BIT);
FITS_TYPE(long, TLONG);
FITS_TYPE(long long, TLONGLONG);
FITS_TYPE(float, TFLOAT);
FITS_TYPE(double, TDOUBLE);
FITS_TYPE(string, TSTRING);

struct FITSColumn {
	fitsfile *file;
	const char *colName;
	int colNum;
	
	FITSColumn(fitsfile *file_, const char *colName_) 
	: file(file_), colName(colName_), colNum(0) {}

	virtual string readStr(int rowNum) = 0;
};

template<typename CTYPE_>
struct FITSTypedColumn : public FITSColumn {
	typedef CTYPE_ CTYPE;
	FITSTypedColumn(fitsfile *file_, const char *colName_) 
	: FITSColumn(file_, colName_) {
		FITS_SAFE(fits_get_colnum(file, CASESEN, const_cast<char*>(colName), &colNum, &status));
		assertColType(file, colNum);
	}

	void assertColType(fitsfile *file, int colNum) {
		int colType = 0;
		long repeat = 0;
		long width = 0;
		FITS_SAFE(fits_get_coltype(file, colNum, &colType, &repeat, &width, &status));
		//cout << "type " << colType << " vs TDOUBLE " << TDOUBLE << " vs TFLOAT " << TFLOAT << endl;
		if (colType != FITSType<CTYPE>::type) throw Exception() << "for column " << colNum << " expected FITS type " << (int)FITSType<CTYPE>::type << " but found " << colType;
		if (repeat != 1) throw Exception() << "for column " << colNum << " expected repeat to be 1 but found " << repeat;
		if (width != sizeof(CTYPE)) throw Exception() << "for column " << colNum << " expected column width to be " << sizeof(CTYPE) << " but found " << width;
	}

	CTYPE read(int rowNum) {
		CTYPE result = CTYPE();
		CTYPE nullValue = CTYPE();
		int nullResult = 0;
		FITS_SAFE(fits_read_col(file, FITSType<CTYPE>::type, colNum, rowNum, 1, 1, &nullValue, &result, &nullResult, &status));
		if (nullResult != 0) throw Exception() << "got nullResult " << nullResult;
		return result;
	}
	
	virtual string readStr(int rowNum) {
		ostringstream ss;
		ss << read(rowNum);
		return ss.str();
	}
};

struct FITSStringColumn : public FITSColumn {
	typedef string CTYPE;
	long width;
	FITSStringColumn(fitsfile *file_, const char *colName_)
	: FITSColumn(file_, colName_) {
		FITS_SAFE(fits_get_colnum(file, CASESEN, const_cast<char*>(colName), &colNum, &status));
		
		int colType = 0;
		long repeat = 0;
		FITS_SAFE(fits_get_coltype(file, colNum, &colType, &repeat, &width, &status));
		if (colType != FITSType<CTYPE>::type) throw Exception() << "expected FITS type " << (int)FITSType<CTYPE>::type << " but found " << colType;
		if (width != repeat * sizeof(char)) throw Exception() << "expected column width to be " << (repeat * sizeof(char)) << " but found " << width;
	}

	CTYPE read(int rowNum) {
		char buffer[256];
		char *result = buffer;
		if (width >= sizeof(buffer)) throw Exception() << "found column that won't fit: needs to be " << width << " bytes";
		
		int nullResult = 0;
		FITS_SAFE(fits_read_col(file, FITSType<CTYPE>::type, colNum, rowNum, 1, 1, NULL, &result, &nullResult, &status));
		if (nullResult != 0) throw Exception() << "got nullResult " << nullResult;
		return string(buffer);

	}
	
	virtual string readStr(int rowNum) {
		return read(rowNum);
	}
};

struct ConvertSDSS3 {
	ConvertSDSS3() {}
	void operator()() {
		const char *sourceFileName = "datasets/gaia/source/results.fits";
		const char *pointDestFileName = "datasets/gaia/points/points.f32";

		mkdir("datasets", 0775);
		mkdir("datasets/gaia", 0775);
		mkdir("datasets/gaia/points", 0775);

		fitsfile *file = NULL;

		FITS_SAFE(fits_open_table(&file, sourceFileName, READONLY, &status));
	
		long numRows = 0;
		FITS_SAFE(fits_get_num_rows(file, &numRows, &status));
		
		int numCols = 0;
		FITS_SAFE(fits_get_num_cols(file, &numCols, &status));

		cout << "numCols: " << numCols << endl;
		cout << "numRows: " << numRows << endl;

		if (getColumns || verbose) {
			int status = 0;
			for (;;) {
				int colNum = 0;
				char colName[256];
				bzero(colName, sizeof(colName));
				fits_get_colname(file, CASESEN, (char *)"*", colName, &colNum, &status);
				if (status == COL_NOT_FOUND) break;
				if (status != 0 && status != COL_NOT_UNIQUE) throw Exception() << fitsGetError(status);
				cout << colNum << "\t" << colName << endl;
			}
			cout << endl;
			if (getColumns) return;
		}
		
		FILE *pointDestFile = NULL;
		if (!omitWrite) {
			pointDestFile = fopen(pointDestFileName, "wb");
			if (!pointDestFile) throw Exception() << "failed to open file " << pointDestFileName << " for writing";
		}

		/*
		what do we want from the archive?
		cx,cy,cz direction
		z redshift <=> velocity <=> distance
		brightness? color? shape?
		catalog name? NGC_*** MS_*** or whatever other identifier?
		*/
		vector<FITSColumn*> columns;
	
		int readStringStartIndex = columns.size();
			
		FITSTypedColumn<double> *col_ra = new FITSTypedColumn<double>(file, "ra"); columns.push_back(col_ra);
		FITSTypedColumn<double> *col_dec = new FITSTypedColumn<double>(file, "dec"); columns.push_back(col_dec);
		FITSTypedColumn<double> *col_parallax = new FITSTypedColumn<double>(file, "parallax"); columns.push_back(col_parallax);

		if (verbose) {
			for (vector<FITSColumn*>::iterator i = columns.begin(); i != columns.end(); ++i) {
				cout << " col num " << (*i)->colName << " = " << (*i)->colNum << endl;
			}
		}	

		Stat stat_ra;
		Stat stat_dec;
		Stat stat_parallax;
		Stat stat_distance;

		//TODO assert columns are of the type  matching what I will be reading

		float vtx[3];
		int numReadable = 0;
		
		//now cycle through rows and pick out values that we want
		if (!interactive) {
			printf("processing     ");
			fflush(stdout);
		}
		time_t lasttime = -1;
		for (int rowNum = 1; rowNum <= numRows; ++rowNum) {
		
			time_t thistime = time(NULL);
			if (!interactive && thistime != lasttime) {
				lasttime = thistime;
				double frac = (double)rowNum / (double)numRows;
				int percent = (int)(100. * sqrt(frac));
				printf("\b\b\b\b%3d%%", percent);
				fflush(stdout);
			}

			double value_ra = col_ra->read(rowNum);
			double value_dec = col_dec->read(rowNum);
			double value_parallax = col_parallax->read(rowNum);
	
			if (verbose) {
				cout << "ra = " << value_ra << endl;
				cout << "dec = " << value_dec << endl;
				cout << "parallax = " << value_parallax << endl;
			}
		
			//distance from parallax
			double distance = 1./value_parallax;	//distance in parsecs ...
			distance *= 1000.;

			if (distance > 0) {

				double rad_ra = value_ra * M_PI / 180.;
				double rad_dec = value_dec * M_PI / 180.;
				double cos_dec = cos(rad_dec);
				vtx[0] = (float)(distance * cos(rad_ra) * cos_dec);
				vtx[1] = (float)(distance * sin(value_ra) * cos_dec);
				vtx[2] = (float)(distance * sin(rad_dec));
				
				if (!isnan(vtx[0]) && !isnan(vtx[1]) && !isnan(vtx[2])
					&& vtx[0] != INFINITY && vtx[0] != -INFINITY 
					&& vtx[1] != INFINITY && vtx[1] != -INFINITY 
					&& vtx[2] != INFINITY && vtx[2] != -INFINITY
				) {
					numReadable++;	
					
					if (showRanges) {
						stat_ra.accum(value_ra, numReadable);
						stat_dec.accum(value_dec, numReadable);
						stat_parallax.accum(value_parallax, numReadable);
						stat_distance.accum(distance, numReadable);
					}
					
					if (!omitWrite) fwrite(vtx, sizeof(vtx), 1, pointDestFile);
				}
			}

			if (verbose) {
				cout << "distance (Mpc) " << distance << endl;
			}

			if (interactive) {
				if (getchar() == 'q') {
					exit(0);
				}
			}
		}
		if (!interactive) {
			printf("\n");
		}

		FITS_SAFE(fits_close_file(file, &status));
		if (!omitWrite) fclose(pointDestFile);

		cout << "num readable: " << numReadable << endl;
	
		if (showRanges) {
			cout 
				<< stat_ra.rw("ra") << endl
				<< stat_dec.rw("dec") << endl
				<< stat_parallax.rw("parallax") << endl
				<< stat_distance.rw("distance") << endl
			;
		}
	}
};

void showhelp() {
	cout
	<< "usage: convert-gaia <options>" << endl
	<< "options:" << endl
	<< "	--verbose				output values" << endl
	<< "	--show-ranges			show ranges of certain fields" << endl
	<< "	--wait					wait for keypress after each entry.  'q' stops" << endl
	<< "	--get-columns			print all column names" << endl
	<< "	--nowrite				don't write results.  useful with --verbose or --read-desc" << endl
	;
}

int main(int argc, char **argv) {
	ConvertSDSS3 convert;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--help")) {
			showhelp();
			return 0;
		} else if (!strcmp(argv[i], "--verbose")) {
			verbose = true;
		} else if (!strcmp(argv[i], "--show-ranges")) {
			showRanges = true;
		} else if (!strcmp(argv[i], "--wait")) {
			verbose = true;
			interactive = true;
		} else if (!strcasecmp(argv[i], "--get-columns")) {
			getColumns = true;
		} else if (!strcasecmp(argv[i], "--nowrite")) {
			omitWrite = true;
		} else {
			showhelp();
			return 0;
		}
	}
	profile("convert-gaia", convert);
}
