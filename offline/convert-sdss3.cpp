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
bool getColumns = false;
bool interactive = false;
bool useMinRedshift = false;
bool readStringDescs = false;
bool trackStrings = false;
bool omitWrite = false;

//notice: "Visualization of large scale structure from the Sloan Digital Sky Survey" by M U SubbaRao, M A Aragón-Calvo, H W Chen, J M Quashnock, A S Szalay and D G York in New Journal of Physics
// samples redshift from 0.01 < z < 0.11
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
		const char *sourceFileName = "datasets/sdss3/source/specObj-dr12.fits";
		const char *pointDestFileName = "datasets/sdss3/points/points.f32";

		mkdir("datasets/sdss3/points", 777);

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
		vector<IFITSTrackBehavior *> trackColumns;

/*
columns:
1	SURVEY
2	INSTRUMENT
3	CHUNK
4	PROGRAMNAME
5	PLATERUN
6	PLATEQUALITY
7	PLATESN2
8	DEREDSN2
9	LAMBDA_EFF
10	BLUEFIBER
11	ZOFFSET
12	SNTURNOFF
13	NTURNOFF
14	SPECPRIMARY
15	SPECSDSS
16	SPECLEGACY
17	SPECSEGUE
18	SPECSEGUE1
19	SPECSEGUE2
20	SPECBOSS
21	BOSS_SPECOBJ_ID
22	SPECOBJID
23	FLUXOBJID
24	BESTOBJID
25	TARGETOBJID
26	PLATEID
27	NSPECOBS
28	FIRSTRELEASE
29	RUN2D
30	RUN1D
31	DESIGNID
32	CX
33	CY
34	CZ
35	XFOCAL
36	YFOCAL
37	SOURCETYPE
38	TARGETTYPE
39	PRIMTARGET
40	SECTARGET
41	LEGACY_TARGET1
42	LEGACY_TARGET2
43	SPECIAL_TARGET1
44	SPECIAL_TARGET2
45	SEGUE1_TARGET1
46	SEGUE1_TARGET2
47	SEGUE2_TARGET1
48	SEGUE2_TARGET2
49	MARVELS_TARGET1
50	MARVELS_TARGET2
51	BOSS_TARGET1
52	BOSS_TARGET2
53	ANCILLARY_TARGET1
54	ANCILLARY_TARGET2
55	SPECTROGRAPHID
56	PLATE
57	TILE
58	MJD
59	FIBERID
60	OBJID
61	PLUG_RA
62	PLUG_DEC
63	CLASS
64	SUBCLASS
65	Z
66	Z_ERR
67	RCHI2
68	DOF
69	RCHI2DIFF
70	TFILE
71	TCOLUMN
72	NPOLY
73	THETA
74	VDISP
75	VDISP_ERR
76	VDISPZ
77	VDISPZ_ERR
78	VDISPCHI2
79	VDISPNPIX
80	VDISPDOF
81	WAVEMIN
82	WAVEMAX
83	WCOVERAGE
84	ZWARNING
85	SN_MEDIAN_ALL
86	SN_MEDIAN
87	CHI68P
88	FRACNSIGMA
89	FRACNSIGHI
90	FRACNSIGLO
91	SPECTROFLUX
92	SPECTROFLUX_IVAR
93	SPECTROSYNFLUX
94	SPECTROSYNFLUX_IVAR
95	SPECTROSKYFLUX
96	ANYANDMASK
97	ANYORMASK
98	SPEC1_G
99	SPEC1_R
100	SPEC1_I
101	SPEC2_G
102	SPEC2_R
103	SPEC2_I
104	ELODIE_FILENAME
105	ELODIE_OBJECT
106	ELODIE_SPTYPE
107	ELODIE_BV
108	ELODIE_TEFF
109	ELODIE_LOGG
110	ELODIE_FEH
111	ELODIE_Z
112	ELODIE_Z_ERR
113	ELODIE_Z_MODELERR
114	ELODIE_RCHI2
115	ELODIE_DOF
116	Z_NOQSO
117	Z_ERR_NOQSO
118	ZWARNING_NOQSO
119	CLASS_NOQSO
120	SUBCLASS_NOQSO
121	RCHI2DIFF_NOQSO
122	Z_PERSON
123	CLASS_PERSON
124	Z_CONF_PERSON
125	COMMENTS_PERSON
126	CALIBFLUX
127	CALIBFLUX_IVAR
*/
	
		int readStringStartIndex = columns.size();
			
		if (readStringDescs) {		//catalog stuff?
			columns.push_back(new FITSStringColumn(file, "SURVEY"));
			columns.push_back(new FITSStringColumn(file, "INSTRUMENT"));
			columns.push_back(new FITSStringColumn(file, "CHUNK"));
			columns.push_back(new FITSStringColumn(file, "PROGRAMNAME"));
			columns.push_back(new FITSStringColumn(file, "PLATERUN"));
			columns.push_back(new FITSStringColumn(file, "PLATEQUALITY"));
			columns.push_back(new FITSTypedColumn<float>(file, "PLATESN2"));
			columns.push_back(new FITSTypedColumn<float>(file, "DEREDSN2"));
			columns.push_back(new FITSTypedColumn<float>(file, "LAMBDA_EFF"));
			columns.push_back(new FITSTypedColumn<int>(file, "BLUEFIBER"));
			columns.push_back(new FITSTypedColumn<float>(file, "ZOFFSET"));
			columns.push_back(new FITSTypedColumn<float>(file, "SNTURNOFF"));
			columns.push_back(new FITSTypedColumn<int>(file, "NTURNOFF"));
			columns.push_back(new FITSTypedColumn<bool>(file, "SPECPRIMARY"));
			columns.push_back(new FITSTypedColumn<bool>(file, "SPECSDSS"));
			columns.push_back(new FITSTypedColumn<bool>(file, "SPECLEGACY"));
			columns.push_back(new FITSTypedColumn<bool>(file, "SPECSEGUE"));
			columns.push_back(new FITSTypedColumn<bool>(file, "SPECSEGUE1"));
			columns.push_back(new FITSTypedColumn<bool>(file, "SPECSEGUE2"));
			columns.push_back(new FITSTypedColumn<bool>(file, "SPECBOSS"));
			columns.push_back(new FITSTypedColumn<int>(file, "BOSS_SPECOBJ_ID"));
			columns.push_back(new FITSStringColumn(file, "SPECOBJID"));
			columns.push_back(new FITSStringColumn(file, "FLUXOBJID"));
			columns.push_back(new FITSStringColumn(file, "BESTOBJID"));
			columns.push_back(new FITSStringColumn(file, "TARGETOBJID"));
			columns.push_back(new FITSStringColumn(file, "PLATEID"));
			columns.push_back(new FITSTypedColumn<short>(file, "NSPECOBS"));
			columns.push_back(new FITSStringColumn(file, "FIRSTRELEASE"));
			columns.push_back(new FITSStringColumn(file, "RUN2D"));
			columns.push_back(new FITSStringColumn(file, "RUN1D"));
			columns.push_back(new FITSTypedColumn<int>(file, "DESIGNID"));
		}
		FITSTypedColumn<double> *col_CX = new FITSTypedColumn<double>(file, "CX"); columns.push_back(col_CX);
		FITSTypedColumn<double> *col_CY = new FITSTypedColumn<double>(file, "CY"); columns.push_back(col_CY);
		FITSTypedColumn<double> *col_CZ = new FITSTypedColumn<double>(file, "CZ"); columns.push_back(col_CZ);
		if (readStringDescs) {
			columns.push_back(new FITSTypedColumn<float>(file, "XFOCAL"));
			columns.push_back(new FITSTypedColumn<float>(file, "YFOCAL"));
		}
		FITSStringTrackColumn *col_SOURCETYPE = new FITSStringTrackColumn(file, "SOURCETYPE"); columns.push_back(col_SOURCETYPE);
		FITSStringTrackColumn *col_TARGETTYPE = new FITSStringTrackColumn(file, "TARGETTYPE"); columns.push_back(col_TARGETTYPE);
		if (readStringDescs) {
			columns.push_back(new FITSTypedColumn<int>(file, "PRIMTARGET"));
			columns.push_back(new FITSTypedColumn<int>(file, "SECTARGET"));
			columns.push_back(new FITSTypedColumn<int>(file, "LEGACY_TARGET1"));
			columns.push_back(new FITSTypedColumn<int>(file, "LEGACY_TARGET2"));
			columns.push_back(new FITSTypedColumn<long long>(file, "SPECIAL_TARGET1"));
			columns.push_back(new FITSTypedColumn<long long>(file, "SPECIAL_TARGET2"));
			columns.push_back(new FITSTypedColumn<int>(file, "SEGUE1_TARGET1"));
			columns.push_back(new FITSTypedColumn<int>(file, "SEGUE1_TARGET2"));
			columns.push_back(new FITSTypedColumn<int>(file, "SEGUE2_TARGET1"));
			columns.push_back(new FITSTypedColumn<int>(file, "SEGUE2_TARGET2"));
			columns.push_back(new FITSTypedColumn<int>(file, "MARVELS_TARGET1"));
			columns.push_back(new FITSTypedColumn<int>(file, "MARVELS_TARGET2"));
			columns.push_back(new FITSTypedColumn<long long>(file, "BOSS_TARGET1"));
			columns.push_back(new FITSTypedColumn<long long>(file, "BOSS_TARGET2"));
			columns.push_back(new FITSTypedColumn<long long>(file, "ANCILLARY_TARGET1"));
			columns.push_back(new FITSTypedColumn<long long>(file, "ANCILLARY_TARGET2"));
			columns.push_back(new FITSTypedColumn<short>(file, "SPECTROGRAPHID"));
			columns.push_back(new FITSTypedColumn<int>(file, "PLATE"));
			columns.push_back(new FITSTypedColumn<int>(file, "TILE"));
			columns.push_back(new FITSTypedColumn<int>(file, "MJD"));
			columns.push_back(new FITSTypedColumn<int>(file, "FIBERID"));
			//columns.push_back(new FITSTypedColumn<int[5]>(file, "OBJID"));
			columns.push_back(new FITSTypedColumn<double>(file, "PLUG_RA"));
			columns.push_back(new FITSTypedColumn<double>(file, "PLUG_DEC"));
		}
		FITSStringTrackColumn *col_CLASS = new FITSStringTrackColumn(file, "CLASS"); columns.push_back(col_CLASS);
		FITSStringTrackColumn *col_SUBCLASS = new FITSStringTrackColumn(file, "SUBCLASS"); columns.push_back(col_SUBCLASS);
		FITSTypedColumn<float> *col_Z = new FITSTypedColumn<float>(file, "Z"); columns.push_back(col_Z);
		if (readStringDescs) {
			columns.push_back(new FITSTypedColumn<float>(file, "Z_ERR"));
			columns.push_back(new FITSTypedColumn<float>(file, "RCHI2"));
			columns.push_back(new FITSTypedColumn<int>(file, "DOF"));
			columns.push_back(new FITSTypedColumn<float>(file, "RCHI2DIFF"));
			columns.push_back(new FITSStringColumn(file, "TFILE"));
			//columns.push_back(new FITSTypedColumn<float[10]>(file, "TCOLUMN"));
			columns.push_back(new FITSTypedColumn<int>(file, "NPOLY"));
			columns.push_back(new FITSStringColumn(file, "TFILE"));
			//columns.push_back(new FITSTypedColumn<float[10]>(file, "THETA"));
			columns.push_back(new FITSTypedColumn<float>(file, "VDISP"));
			columns.push_back(new FITSTypedColumn<float>(file, "VDISP_ERR"));
			columns.push_back(new FITSTypedColumn<float>(file, "VDISPZ"));
			columns.push_back(new FITSTypedColumn<float>(file, "VDISPZ_ERR"));
			columns.push_back(new FITSTypedColumn<int>(file, "VDISPDOF"));
			columns.push_back(new FITSTypedColumn<float>(file, "VDISPCHI2"));
			columns.push_back(new FITSTypedColumn<float>(file, "VDISPNPIX"));
			columns.push_back(new FITSTypedColumn<float>(file, "WAVEMIN"));
			columns.push_back(new FITSTypedColumn<float>(file, "WAVEMAX"));
			columns.push_back(new FITSTypedColumn<float>(file, "WCOVERAGE"));
			columns.push_back(new FITSTypedColumn<int>(file, "ZWARNING"));
			columns.push_back(new FITSTypedColumn<float>(file, "SN_MEDIAN_ALL"));
			//columns.push_back(new FITSTypedColumn<float[5]>(file, "SN_MEDIAN"));
			columns.push_back(new FITSTypedColumn<float>(file, "CHI68P"));
			//columns.push_back(new FITSTypedColumn<float[10]>(file, "FRACNSIGMA"));
			//columns.push_back(new FITSTypedColumn<float[10]>(file, "FRACNSIGHI"));
			//columns.push_back(new FITSTypedColumn<float[10]>(file, "FRACNSIGLO"));
			//columns.push_back(new FITSTypedColumn<float[5]>(file, "SPECTROFLUX"));
			//columns.push_back(new FITSTypedColumn<float[5]>(file, "SPECTROFLUX_IVAR"));
			//columns.push_back(new FITSTypedColumn<float[5]>(file, "SPECTROSYNFLUX"));
			//columns.push_back(new FITSTypedColumn<float[5]>(file, "SPECTROSYNFLUX_IVAR"));
			//columns.push_back(new FITSTypedColumn<float[5]>(file, "SPECTROSKYFLUX"));
			columns.push_back(new FITSTypedColumn<int>(file, "ANYANDMASK"));
			columns.push_back(new FITSTypedColumn<int>(file, "ANYORMASK"));
			columns.push_back(new FITSTypedColumn<float>(file, "SPEC1_G"));
			columns.push_back(new FITSTypedColumn<float>(file, "SPEC1_R"));
			columns.push_back(new FITSTypedColumn<float>(file, "SPEC1_I"));
			columns.push_back(new FITSTypedColumn<float>(file, "SPEC2_G"));
			columns.push_back(new FITSTypedColumn<float>(file, "SPEC2_R"));
			columns.push_back(new FITSTypedColumn<float>(file, "SPEC2_I"));
			columns.push_back(new FITSStringColumn(file, "ELODIE_FILENAME"));
			columns.push_back(new FITSStringColumn(file, "ELODIE_OBJECT"));
			columns.push_back(new FITSStringColumn(file, "ELODIE_SPTYPE"));
			columns.push_back(new FITSTypedColumn<float>(file, "ELODIE_BV"));
			columns.push_back(new FITSTypedColumn<float>(file, "ELODIE_TEFF"));
			columns.push_back(new FITSTypedColumn<float>(file, "ELODIE_LOGG"));
			columns.push_back(new FITSTypedColumn<float>(file, "ELODIE_FEH"));
			columns.push_back(new FITSTypedColumn<float>(file, "ELODIE_Z"));
			columns.push_back(new FITSTypedColumn<float>(file, "ELODIE_Z_ERR"));
			columns.push_back(new FITSTypedColumn<float>(file, "ELODIE_Z_MODELERR"));
			columns.push_back(new FITSTypedColumn<float>(file, "ELODIE_RCHI2"));
			columns.push_back(new FITSTypedColumn<int>(file, "ELODIE_DOF"));
			columns.push_back(new FITSTypedColumn<float>(file, "Z_NOQSO"));
			columns.push_back(new FITSTypedColumn<float>(file, "Z_ERR_NOQSO"));
			columns.push_back(new FITSTypedColumn<int>(file, "ZWARNING_NOQSO"));
			columns.push_back(new FITSStringColumn(file, "CLASS_NOQSO"));
			columns.push_back(new FITSStringColumn(file, "SUBCLASS_NOQSO"));
			columns.push_back(new FITSTypedColumn<float>(file, "RCHI2DIFF_NOQSO"));
			columns.push_back(new FITSTypedColumn<float>(file, "Z_PERSON"));
			columns.push_back(new FITSTypedColumn<int>(file, "CLASS_PERSON"));
			columns.push_back(new FITSTypedColumn<int>(file, "Z_CONF_PERSON"));
			columns.push_back(new FITSStringColumn(file, "COMMENTS_PERSON"));
			//columns.push_back(new FITSTypedColumn<float[5]>(file, "CALIBFLUX"));
			//columns.push_back(new FITSTypedColumn<float[5]>(file, "CALIBFLUX_IVAR"));
		}

		if (trackStrings) {
			trackColumns.push_back(col_SOURCETYPE);
			trackColumns.push_back(col_TARGETTYPE);
			trackColumns.push_back(col_CLASS);
			trackColumns.push_back(col_SUBCLASS);
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
				if (!omitWrite) fwrite(vtx, sizeof(vtx), 1, pointDestFile);
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
		if (!omitWrite) fclose(pointDestFile);
		
		cout << "num readable: " << numReadable << endl;
	}
};

void showhelp() {
	cout
	<< "usage: convert-sdss3 <options>" << endl
	<< "options:" << endl
	<< "	--verbose				output values" << endl
	<< "	--wait					wait for keypress after each entry.  'q' stops" << endl
	<< "	--read-desc				reads string descriptions" << endl
	<< "	--min-redshift <cz> 	specify minimum redshift" << endl
	<< "	--enum-class			enumerate all classes" << endl
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
		} else if (!strcasecmp(argv[i], "--get-columns")) {
			getColumns = true;
		} else if (!strcasecmp(argv[i], "--nowrite")) {
			omitWrite = true;
		} else {
			showhelp();
			return 0;
		}
	}
	profile("convert-sdss3", convert);
}

