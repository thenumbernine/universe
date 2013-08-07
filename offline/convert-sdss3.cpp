/*
usage:
convert-sdss3			generates point file
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

#include "exception.h"
#include "util.h"
#include "defs.h"

using namespace std;

bool verbose = false;
bool interactive = false;
bool useMinRedshift = false;
bool readStringDescs = false;
bool trackStrings = false;
double minRedshift = -numeric_limits<double>::infinity();

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
		if (colType != FITSType<CTYPE>::type) throw Exception() << "expected FITS type " << (int)FITSType<CTYPE>::type << " but found " << colType;
		if (repeat != 1) throw Exception() << "expected repeat to be 1 but found " << repeat;
		if (width != sizeof(CTYPE)) throw Exception() << "expected column width to be " << sizeof(CTYPE) << " but found " << width;
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

struct IFITSTrackBehavior {
	virtual void track(int rowNum) = 0;
	virtual void printAll() = 0;
};

template<typename PARENT>
struct FITSTrackBehavior : public PARENT, public IFITSTrackBehavior {
	map<typename PARENT::CTYPE, int> valueSet;
	FITSTrackBehavior(fitsfile *file_, const char *colName_) : PARENT(file_, colName_) {}

	void track(int rowNum) {
		++valueSet[PARENT::read(rowNum)];
	}

	void printAll() {
		cout << "values of " << PARENT::colName << ":" << endl;
		for (map<string, int>::iterator i = valueSet.begin(); i != valueSet.end(); ++i) {
			cout << i->first << " : " << i->second << endl;
		}
		cout << endl;
	}
};

typedef FITSTrackBehavior<FITSStringColumn> FITSStringTrackColumn;

struct ConvertSDSS3 {
	ConvertSDSS3() {}
	void operator()() {
		const char *sourceFileName = "datasets/sdss3/source/specObj-dr9.fits";
		const char *pointDestFileName = "datasets/sdss3/points/points.f32";

		mkdir("datasets/sdss3/points", 777);

		FILE *pointDestFile = fopen(pointDestFileName, "wb");
		if (!pointDestFile) throw Exception() << "failed to open file " << pointDestFileName;

		fitsfile *file = NULL;

		FITS_SAFE(fits_open_table(&file, sourceFileName, READONLY, &status));
	
		long numRows = 0;
		FITS_SAFE(fits_get_num_rows(file, &numRows, &status));
		
		int numCols = 0;
		FITS_SAFE(fits_get_num_cols(file, &numCols, &status));

		cout << "numCols: " << numCols << endl;
		cout << "numRows: " << numRows << endl;

		if (verbose) {
			int status = 0;
			for (;;) {
				int colNum = 0;
				char colName[256];
				bzero(colName, sizeof(colName));
				fits_get_colname(file, CASESEN, "*", colName, &colNum, &status);
				if (status == COL_NOT_FOUND) break;
				if (status != 0 && status != COL_NOT_UNIQUE) throw Exception() << fitsGetError(status);
				cout << colNum << "\t" << colName << endl;
			}
			cout << endl;
		}

		/*
		what do we want from the archive?
		cx,cy,cz direction
		z redshift <=> velocity <=> distance
		brightness? color? shape?
		catalog name? NGC_*** MS_*** or whatever other identifier?
		*/
		vector<FITSColumn*> columns;
		vector<IFITSTrackBehavior *> trackColumns;

		FITSTypedColumn<double> *col_CX = new FITSTypedColumn<double>(file, "CX"); columns.push_back(col_CX);
		FITSTypedColumn<double> *col_CY = new FITSTypedColumn<double>(file, "CY"); columns.push_back(col_CY);
		FITSTypedColumn<double> *col_CZ = new FITSTypedColumn<double>(file, "CZ"); columns.push_back(col_CZ);
		FITSTypedColumn<float> *col_Z = new FITSTypedColumn<float>(file, "Z"); columns.push_back(col_Z);
		
		int readStringStartIndex = columns.size();
		
		FITSStringTrackColumn *col_SOURCETYPE = new FITSStringTrackColumn(file, "SOURCETYPE"); columns.push_back(col_SOURCETYPE);
		FITSStringTrackColumn *col_TARGETTYPE = new FITSStringTrackColumn(file, "TARGETTYPE"); columns.push_back(col_TARGETTYPE);
		FITSStringTrackColumn *col_CLASS = new FITSStringTrackColumn(file, "CLASS"); columns.push_back(col_CLASS);
		FITSStringTrackColumn *col_SUBCLASS = new FITSStringTrackColumn(file, "SUBCLASS"); columns.push_back(col_SUBCLASS);
		if (trackStrings) {
			trackColumns.push_back(col_SOURCETYPE);
			trackColumns.push_back(col_TARGETTYPE);
			trackColumns.push_back(col_CLASS);
			trackColumns.push_back(col_SUBCLASS);
		}

		if (readStringDescs ) {		//catalog stuff?
			columns.push_back(new FITSStringColumn(file, "SURVEY"));
			columns.push_back(new FITSStringColumn(file, "INSTRUMENT"));
			columns.push_back(new FITSStringColumn(file, "CHUNK"));
			columns.push_back(new FITSStringColumn(file, "PROGRAMNAME"));
			columns.push_back(new FITSStringColumn(file, "PLATERUN"));
			columns.push_back(new FITSStringColumn(file, "PLATEQUALITY"));

			columns.push_back(new FITSStringColumn(file, "SPECOBJID"));
			columns.push_back(new FITSStringColumn(file, "FLUXOBJID"));
			columns.push_back(new FITSStringColumn(file, "BESTOBJID"));
			columns.push_back(new FITSStringColumn(file, "TARGETOBJID"));
			columns.push_back(new FITSStringColumn(file, "PLATEID"));
			columns.push_back(new FITSStringColumn(file, "FIRSTRELEASE"));
			columns.push_back(new FITSStringColumn(file, "RUN2D"));
			columns.push_back(new FITSStringColumn(file, "RUN1D"));

			columns.push_back(new FITSStringColumn(file, "TFILE"));
			columns.push_back(new FITSStringColumn(file, "ELODIE_FILENAME"));
			columns.push_back(new FITSStringColumn(file, "ELODIE_OBJECT"));
			columns.push_back(new FITSStringColumn(file, "ELODIE_SPTYPE"));
			columns.push_back(new FITSStringColumn(file, "CLASS_NOQSO"));
			columns.push_back(new FITSStringColumn(file, "SUBCLASS_NOQSO"));
			columns.push_back(new FITSStringColumn(file, "COMMENTS_PERSON"));
		}

		if (verbose) {
			for (vector<FITSColumn*>::iterator i = columns.begin(); i != columns.end(); ++i) {
				cout << " col num " << (*i)->colName << " = " << (*i)->colNum << endl;
			}
		}	

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

			double value_CX = col_CX->read(rowNum);
			double value_CY = col_CY->read(rowNum);
			double value_CZ = col_CZ->read(rowNum);
			float value_Z = col_Z->read(rowNum);
			string value_CLASS = col_CLASS->read(rowNum);

			for (vector<IFITSTrackBehavior*>::iterator i = trackColumns.begin(); i != trackColumns.end(); ++i) {
				(*i)->track(rowNum);
			}
			
			//galaxies only for now
			if (value_CLASS != "GALAXY") continue;
			
			if (verbose) {
				cout << "CX = " << value_CX << endl;
				cout << "CY = " << value_CY << endl;
				cout << "CZ = " << value_CZ << endl;
				cout << "Z = " << value_Z << endl;
			}
			
			if (readStringDescs ) {	//catalog stuff
				for (vector<FITSColumn*>::iterator i = columns.begin() + readStringStartIndex; i != columns.end(); ++i) {
					cout << (*i)->colName << " = " << (*i)->readStr(rowNum) << endl;
				}
			}

			if (useMinRedshift && value_Z < minRedshift) continue;
			
		
			double redshift = SPEED_OF_LIGHT * value_Z;
			//redshift is in km/s
			//distance is in Mpc
			double distance = redshift / HUBBLE_CONSTANT;
			vtx[0] = (float)(distance * value_CX); 
			vtx[1] = (float)(distance * value_CY); 
			vtx[2] = (float)(distance * value_CZ);

			if (!isnan(vtx[0]) && !isnan(vtx[1]) && !isnan(vtx[2])
				&& vtx[0] != INFINITY && vtx[0] != -INFINITY 
				&& vtx[1] != INFINITY && vtx[1] != -INFINITY 
				&& vtx[2] != INFINITY && vtx[2] != -INFINITY
			) {
				numReadable++;	
				fwrite(vtx, sizeof(vtx), 1, pointDestFile);
			}

			if (verbose) {
				cout << "redshift (km/s) " << redshift << endl;
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

		for (vector<IFITSTrackBehavior*>::iterator i = trackColumns.begin(); i != trackColumns.end(); ++i) {
			(*i)->printAll();
		}

		FITS_SAFE(fits_close_file(file, &status));
		fclose(pointDestFile);
		
		cout << "num readable: " << numReadable << endl;
	}
};

void showhelp() {
	cout
	<< "usage: convert-sdss3 <options>" << endl
	<< "options:" << endl
	<< "	--verbose				output values" << endl
	<< "	--wait					wait for keypress after each entry.  'q' stops" << endl
	<<"		--read-desc				reads string descriptions" << endl
	<< "	--min-redshift <cz> 	specify minimum redshift" << endl
	<< "	--enum-class			enumerate all classes" << endl
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
		} else if (!strcmp(argv[i], "--wait")) {
			verbose = true;
			interactive = true;
		} else if (!strcmp(argv[i], "--min-redshift") && i < argc-1) {
			useMinRedshift = true;
			minRedshift = atof(argv[++i]);
		} else if (!strcasecmp(argv[i], "--read-desc")) {
			readStringDescs  = true;
		} else if (!strcasecmp(argv[i], "--enum-class")) {
			trackStrings = true;
		} else {
			showhelp();
			return 0;
		}
	}
	profile("convert-sdss3", convert);
}

