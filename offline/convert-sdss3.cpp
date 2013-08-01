/*
usage:
convert-sdss3			generates point file
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

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
double minRedshift = -numeric_limits<double>::infinity();

#define ECHO(x)	cout << #x << " " << x << endl

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

//general case.  check might as well be merged with the function.
template<typename CTYPE>
void fitsAssertColType(fitsfile *file, int colNum) {
	int colType = 0;
	long repeat = 0;
	long width = 0;
	FITS_SAFE(fits_get_coltype(file, colNum, &colType, &repeat, &width, &status));
	//cout << "type " << colType << " vs TDOUBLE " << TDOUBLE << " vs TFLOAT " << TFLOAT << endl;
	if (colType != FITSType<CTYPE>::type) throw Exception() << "expected FITS type " << (int)FITSType<CTYPE>::type << " but found " << colType;
	if (repeat != 1) throw Exception() << "expected repeat to be 1 but found " << repeat;
	if (width != sizeof(CTYPE)) throw Exception() << "expected column width to be " << sizeof(CTYPE) << " but found " << width;
}

template<typename CTYPE>
int fitsSafeGetColumn(fitsfile *file, const char *colName) {
	int colNum = 0;
	FITS_SAFE(fits_get_colnum(file, CASESEN, const_cast<char*>(colName), &colNum, &status));
	fitsAssertColType<CTYPE>(file, colNum);
	return colNum;
}

//string specialization.  check needs to return a variable so it is merged.  i could have it always return....
int fitsSafeGetStringColumn(fitsfile *file, const char *colName, long *width) {
	int colNum = 0;
	FITS_SAFE(fits_get_colnum(file, CASESEN, const_cast<char*>(colName), &colNum, &status));
	
	typedef string CTYPE;
	int colType = 0;
	long repeat = 0;
	FITS_SAFE(fits_get_coltype(file, colNum, &colType, &repeat, width, &status));
	if (colType != FITSType<CTYPE>::type) throw Exception() << "expected FITS type " << (int)FITSType<CTYPE>::type << " but found " << colType;
	if (*width != repeat * sizeof(char)) throw Exception() << "expected column width to be " << (repeat * sizeof(char)) << " but found " << *width;

	return colNum;
}
//generic type
template<typename CTYPE>
CTYPE fitsReadValue(fitsfile *file, int colNum, int rowNum);

//floats have NAN-checking built in
template<typename CTYPE>
CTYPE fitsReadFloatValue(fitsfile *file, int colNum, int rowNum) {
	CTYPE result = NAN;
	CTYPE nullValue = NAN;
	int nullResult = 0;
	FITS_SAFE(fits_read_col(file, FITSType<CTYPE>::type, colNum, rowNum, 1, 1, &nullValue, &result, &nullResult, &status));
	if (nullResult != 0) throw Exception() << "got nullResult " << nullResult;
	return result;
}

template<> float fitsReadValue<float>(fitsfile *file, int colNum, int rowNum) {
	return fitsReadFloatValue<float>(file, colNum, rowNum);
}

template<> double fitsReadValue<double>(fitsfile *file, int colNum, int rowNum) {
	return fitsReadFloatValue<float>(file, colNum, rowNum);
}

//strings pass around byte buffers
string fitsReadStringValue(fitsfile *file, int colNum, int rowNum, int len) {

	typedef string CTYPE;
	char buffer[256];
	char *result = buffer;
	if (len >= sizeof(buffer)) throw Exception() << "found column that won't fit: needs to be " << len << " bytes";
	
	int nullResult = 0;
	FITS_SAFE(fits_read_col(file, FITSType<CTYPE>::type, colNum, rowNum, 1, 1, NULL, &result, &nullResult, &status));
	if (nullResult != 0) throw Exception() << "got nullResult " << nullResult;
	return string(buffer);
}

//some ugly macros I'm making too much use of
#define FITS_SAFE_GET_COLUMN(CTYPE,COLUMN)		int colNum_##COLUMN = fitsSafeGetColumn<CTYPE>(file, #COLUMN)
#define FITS_SAFE_READ_COLUMN(CTYPE, COLUMN)	CTYPE value_##COLUMN = fitsReadValue<CTYPE>(file, colNum_##COLUMN, rowNum);	
#define FITS_SAFE_GET_STRING_COLUMN(COLUMN)		long len_##COLUMN = 0; int colNum_##COLUMN = fitsSafeGetStringColumn(file, #COLUMN, &len_##COLUMN);
#define FITS_SAFE_READ_STRING_COLUMN(COLUMN)	string value_##COLUMN = fitsReadStringValue(file, colNum_##COLUMN, rowNum, len_##COLUMN);	

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

		FITS_SAFE_GET_COLUMN(double, CX);
		FITS_SAFE_GET_COLUMN(double, CY);
		FITS_SAFE_GET_COLUMN(double, CZ);
		FITS_SAFE_GET_COLUMN(float, Z);
		
		//catalog stuff?
		FITS_SAFE_GET_STRING_COLUMN(SURVEY);
		FITS_SAFE_GET_STRING_COLUMN(INSTRUMENT);
		FITS_SAFE_GET_STRING_COLUMN(CHUNK);
		FITS_SAFE_GET_STRING_COLUMN(PROGRAMNAME);
		FITS_SAFE_GET_STRING_COLUMN(PLATERUN);
		FITS_SAFE_GET_STRING_COLUMN(PLATEQUALITY);

		FITS_SAFE_GET_STRING_COLUMN(SPECOBJID);
		FITS_SAFE_GET_STRING_COLUMN(FLUXOBJID);
		FITS_SAFE_GET_STRING_COLUMN(BESTOBJID);
		FITS_SAFE_GET_STRING_COLUMN(TARGETOBJID);
		FITS_SAFE_GET_STRING_COLUMN(PLATEID);
		FITS_SAFE_GET_STRING_COLUMN(FIRSTRELEASE);
		FITS_SAFE_GET_STRING_COLUMN(RUN2D);
		FITS_SAFE_GET_STRING_COLUMN(RUN1D);


		if (verbose) {
			ECHO(colNum_CX);
			ECHO(colNum_CY);
			ECHO(colNum_CZ);
			ECHO(colNum_Z);
			
			//catalog	
			ECHO(colNum_SURVEY);
			ECHO(colNum_INSTRUMENT);
			ECHO(colNum_CHUNK);
			ECHO(colNum_PROGRAMNAME);
			ECHO(colNum_PLATERUN);
			ECHO(colNum_PLATEQUALITY);
	
			ECHO(colNum_SPECOBJID);
			ECHO(colNum_FLUXOBJID);
			ECHO(colNum_BESTOBJID);
			ECHO(colNum_TARGETOBJID);
			ECHO(colNum_PLATEID);
			ECHO(colNum_FIRSTRELEASE);
			ECHO(colNum_RUN2D);
			ECHO(colNum_RUN1D);
		}	

		//TODO assert columns are of the type  matching what I will be reading

		//now cycle through rows and pick out values that we want
		for (int rowNum = 1; rowNum <= numRows; ++rowNum) {
			FITS_SAFE_READ_COLUMN(double, CX);
			FITS_SAFE_READ_COLUMN(double, CY);
			FITS_SAFE_READ_COLUMN(double, CZ);
			FITS_SAFE_READ_COLUMN(float, Z);
			
			//catalog stuff?
			FITS_SAFE_READ_STRING_COLUMN(SURVEY);
			FITS_SAFE_READ_STRING_COLUMN(INSTRUMENT);
			FITS_SAFE_READ_STRING_COLUMN(CHUNK);
			FITS_SAFE_READ_STRING_COLUMN(PROGRAMNAME);
			FITS_SAFE_READ_STRING_COLUMN(PLATERUN);
			FITS_SAFE_READ_STRING_COLUMN(PLATEQUALITY);

			FITS_SAFE_READ_STRING_COLUMN(SPECOBJID);
			FITS_SAFE_READ_STRING_COLUMN(FLUXOBJID);
			FITS_SAFE_READ_STRING_COLUMN(BESTOBJID);
			FITS_SAFE_READ_STRING_COLUMN(TARGETOBJID);
			FITS_SAFE_READ_STRING_COLUMN(PLATEID);
			FITS_SAFE_READ_STRING_COLUMN(FIRSTRELEASE);
			FITS_SAFE_READ_STRING_COLUMN(RUN2D);
			FITS_SAFE_READ_STRING_COLUMN(RUN1D);

			if (verbose) {
				ECHO(value_CX);
				ECHO(value_CY);
				ECHO(value_CZ);
				ECHO(value_Z);
				
				//catalog stuff?
				ECHO(value_SURVEY);
				ECHO(value_INSTRUMENT);
				ECHO(value_CHUNK);
				ECHO(value_PROGRAMNAME);
				ECHO(value_PLATERUN);
				ECHO(value_PLATEQUALITY);
			
				ECHO(value_SPECOBJID);
				ECHO(value_FLUXOBJID);
				ECHO(value_BESTOBJID);
				ECHO(value_TARGETOBJID);
				ECHO(value_PLATEID);
				ECHO(value_FIRSTRELEASE);
				ECHO(value_RUN2D);
				ECHO(value_RUN1D);
			}

			if (interactive) {
				if (getchar() == 'q') {
					exit(0);
				}
			}
		}


		FITS_SAFE(fits_close_file(file, &status));
	}
};

void showhelp() {
	cout
	<< "usage: convert-sdss3 <options>" << endl
	<< "options:" << endl
	<< "	--verbose	output values" << endl
	<< "	--wait		wait for keypress after each entry.  'q' stops" << endl
	<< "	--min-redshift <cz> 	specify minimum redshift" << endl
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
		} else {
			showhelp();
			return 0;
		}
	}
	profile("convert-sdss3", convert);
}

