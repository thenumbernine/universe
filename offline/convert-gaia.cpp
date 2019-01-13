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
bool outputExtra = true;
bool keepNegativeParallax = false;

//fits is rigid and I am lazy.  use its writer to stderr and return the same status
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

template<typename T> struct GetOutputExt;
template<> struct GetOutputExt<float> { static std::string exec() { return "f32"; } };
template<> struct GetOutputExt<double> { static std::string exec() { return "f64"; } };

template<typename OutputPrecision>
struct ConvertSDSS3 {
	std::string getOutputExt() const { 
		return GetOutputExt<OutputPrecision>::exec(); 
	}
	
	ConvertSDSS3() {}
	
	void operator()() {
		std::string pointDestFileName = std::string("datasets/gaia/points/points.") + getOutputExt();
		
		FILE *pointDestFile = NULL;
		if (!omitWrite) {
			pointDestFile = fopen(pointDestFileName.c_str(), "wb");
			if (!pointDestFile) throw Exception() << "failed to open file " << pointDestFileName << " for writing";
		}
	
		Stat stat_ra;
		Stat stat_dec;
		Stat stat_parallax;
		Stat stat_pmra;
		Stat stat_pmdec;
		Stat stat_radial_velocity;
		Stat stat_teff_val;
		Stat stat_radius_val;
		Stat stat_lum_val;
		//not a complete 'stat', just min/max
		long long source_id_min = -std::numeric_limits<long long>::infinity();
		long long source_id_max = std::numeric_limits<long long>::infinity();

		//derived:
		Stat stat_distance;

		//TODO assert columns are of the type  matching what I will be reading

		OutputPrecision position[3], velocity[3], radius, temp, luminosity;
		int numReadable = 0;
	

		for (const std::string& sourceFileName : std::vector<std::string>{
			"datasets/gaia/source/1.fits",
			"datasets/gaia/source/2.fits",
			"datasets/gaia/source/3.fits",
		}) {

			mkdir("datasets", 0775);
			mkdir("datasets/gaia", 0775);
			mkdir("datasets/gaia/points", 0775);

			fitsfile *file = NULL;

			FITS_SAFE(fits_open_table(&file, sourceFileName.c_str(), READONLY, &status));
		
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


			/*
			what do we want from the archive?
			cx,cy,cz direction
			z redshift <=> velocity <=> distance
			brightness? color? shape?
			catalog name? NGC_*** MS_*** or whatever other identifier?
			*/
			vector<FITSColumn*> columns;
		
			int readStringStartIndex = columns.size();

			//source_id
			FITSTypedColumn<long long> *col_source_id = new FITSTypedColumn<long long>(file, "source_id"); columns.push_back(col_source_id);
			//right ascension (radians)
			FITSTypedColumn<double> *col_ra = new FITSTypedColumn<double>(file, "ra"); columns.push_back(col_ra);
			//declination (radians)
			FITSTypedColumn<double> *col_dec = new FITSTypedColumn<double>(file, "dec"); columns.push_back(col_dec);
			//parallax (radians)
			FITSTypedColumn<double> *col_parallax = new FITSTypedColumn<double>(file, "parallax"); columns.push_back(col_parallax);
			//proper motion in right ascension (radians/year)
			FITSTypedColumn<double> *col_pmra = nullptr; if (outputExtra) { col_pmra = new FITSTypedColumn<double>(file, "pmra"); columns.push_back(col_pmra); }
			//proper motion in declination (radians/year)
			FITSTypedColumn<double> *col_pmdec = nullptr; if (outputExtra) { col_pmdec = new FITSTypedColumn<double>(file, "pmdec"); columns.push_back(col_pmdec); }
			//radial velocity
			FITSTypedColumn<double> *col_radial_velocity = nullptr; if (outputExtra) { col_radial_velocity = new FITSTypedColumn<double>(file, "radial_velocity"); columns.push_back(col_radial_velocity); }
			//stellar effective temperature (K)
			FITSTypedColumn<float> *col_teff_val = nullptr; if (outputExtra) { col_teff_val = new FITSTypedColumn<float>(file, "teff_val"); columns.push_back(col_teff_val); }
			//stellar radius (solar radii)
			FITSTypedColumn<float> *col_radius_val = nullptr; if (outputExtra) { col_radius_val = new FITSTypedColumn<float>(file, "radius_val"); columns.push_back(col_radius_val); }
			//stellar luminosity (solar luminosity)
			FITSTypedColumn<float> *col_lum_val = nullptr; if (outputExtra) { col_lum_val = new FITSTypedColumn<float>(file, "lum_val"); columns.push_back(col_lum_val); }

			//well that's 9 columns.  I wasn't storing radius in the HYG data

			if (verbose) {
				for (vector<FITSColumn*>::iterator i = columns.begin(); i != columns.end(); ++i) {
					cout << " col num " << (*i)->colName << " = " << (*i)->colNum << endl;
				}
			}	

		
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

				long long value_source_id = col_source_id->read(rowNum);
				source_id_min = std::min(source_id_min, value_source_id);
				source_id_max = std::max(source_id_max, value_source_id);

				double value_ra = col_ra->read(rowNum);
				double value_dec = col_dec->read(rowNum);
				double value_parallax = col_parallax->read(rowNum);
				
				
				double value_pmra, value_pmdec, value_radial_velocity, value_teff_val, value_radius_val, value_lum_val;
				if (outputExtra) {
					value_pmra = col_pmra->read(rowNum);
					value_pmdec = col_pmdec->read(rowNum);
					value_radial_velocity = col_radial_velocity->read(rowNum);
					value_teff_val = col_teff_val->read(rowNum);
					value_radius_val = col_radius_val->read(rowNum);
					value_lum_val = col_lum_val->read(rowNum);
				
					radius = value_radius_val;
					temp = value_teff_val;
					luminosity = value_lum_val;
				}

				if (verbose) {
					cout << endl;
					cout << "source_id = " << value_source_id << endl;
					cout << "ra = " << value_ra << endl;
					cout << "dec = " << value_dec << endl;
					cout << "parallax = " << value_parallax << endl;
					if (outputExtra) {
						cout << "pmra = " << value_pmra << endl;
						cout << "pmdec = " << value_pmdec << endl;
						cout << "radial_velocity = " << value_radial_velocity << endl;
						cout << "teff_val = " << value_teff_val << endl;
						cout << "radius_val = " << value_radius_val << endl;
						cout << "lum_val = " << value_lum_val << endl;
					}
				}
			
				//distance from parallax
				double arcsec_parallax = value_parallax * .001;	//convert from milliarcseconds to arcseconds
				double distance = 1./arcsec_parallax;	//convert from arcseconds to parsecs 
				// comment this for universe visualizer
				//distance *= .0001;	//parsec to kparsec

				if (keepNegativeParallax || distance > 0) {	// what do we do with negative parallax?

					double rad_ra = value_ra * M_PI / 180.;
					double rad_dec = value_dec * M_PI / 180.;
					double cos_dec = cos(rad_dec);
					double sin_dec = sin(rad_dec);
					double cos_ra = cos(rad_ra);
					double sin_ra = sin(rad_ra);
					position[0] = (OutputPrecision)(distance * cos_dec * cos_ra);
					position[1] = (OutputPrecision)(distance * cos_dec * sin_ra);
					position[2] = (OutputPrecision)(distance * sin_dec);

					if (outputExtra) {
						//dec = theta
						//ra = phi
						OutputPrecision 
							e_r_x = sin_dec * cos_ra,
							e_r_y = sin_dec * sin_ra,
							e_r_z = cos_dec,
							e_dec_x = cos_dec * cos_ra,
							e_dec_y = cos_dec * sin_ra,
							e_dec_z = -sin_dec,
							e_ra_x = -sin_ra,
							e_ra_y = cos_ra,
							e_ra_z = 0.;
						
						OutputPrecision pmra_parsec_year = 1./value_pmra;  // or would this be parsec-years?
						OutputPrecision pmra_m_s = 0, pmdec_m_s = 0, radial_velocity_m_s = 0;
						velocity[0] = (OutputPrecision)(e_ra_x * pmra_m_s + e_dec_x * pmdec_m_s + e_r_x * radial_velocity_m_s);
						velocity[1] = (OutputPrecision)(e_ra_y * pmra_m_s + e_dec_y * pmdec_m_s + e_r_x * radial_velocity_m_s);
						velocity[2] = (OutputPrecision)(e_ra_z * pmra_m_s + e_dec_z * pmdec_m_s + e_r_x * radial_velocity_m_s);
					}
					
					if (!isnan(position[0]) && !isnan(position[1]) && !isnan(position[2])
						&& position[0] != INFINITY && position[0] != -INFINITY 
						&& position[1] != INFINITY && position[1] != -INFINITY 
						&& position[2] != INFINITY && position[2] != -INFINITY
					) {
						numReadable++;	
						
						if (showRanges) {
							stat_ra.accum(value_ra, numReadable);
							stat_dec.accum(value_dec, numReadable);
							stat_parallax.accum(value_parallax, numReadable);
							stat_distance.accum(distance, numReadable);
							if (outputExtra) {
								stat_pmra.accum(value_pmra, numReadable);
								stat_pmdec.accum(value_pmdec, numReadable);
								stat_radial_velocity.accum(value_radial_velocity, numReadable);
								stat_teff_val.accum(value_teff_val, numReadable);
								stat_radius_val.accum(value_radius_val, numReadable);
								stat_lum_val.accum(value_lum_val, numReadable);
							}
						}
						
						if (!omitWrite) {
							fwrite(position, sizeof(position), 1, pointDestFile);
							fwrite(velocity, sizeof(velocity), 1, pointDestFile);
							fwrite(&radius, sizeof(radius), 1, pointDestFile);
							fwrite(&temp, sizeof(temp), 1, pointDestFile);
							fwrite(&luminosity, sizeof(luminosity), 1, pointDestFile);
						}
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
		}

		if (!omitWrite) fclose(pointDestFile);

		cout << "num readable: " << numReadable << endl;
	
		if (showRanges) {
			cout 
				<< stat_ra.rw("ra") << endl
				<< stat_dec.rw("dec") << endl
				<< stat_parallax.rw("parallax") << endl
				<< stat_pmra.rw("pmra") << endl
				<< stat_pmdec.rw("pmdec") << endl
				<< stat_radial_velocity.rw("radial_velocity") << endl
				<< stat_teff_val.rw("teff_val") << endl
				<< stat_radius_val.rw("radius_val") << endl
				<< stat_lum_val.rw("lum_val") << endl
				<< stat_distance.rw("distance") << endl
			;
		}

		std::cout << "source_id range " << source_id_min << " to " << source_id_max << std::endl;
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
	<< "	--output-extra			also output velocity, temperature, and luminosity" << endl
	<< "	--keep-neg-parallax		keep negative parallax" << endl
	<< "	--double				output as double precision (default single)" << endl
	;
}

int main(int argc, char **argv) {
	bool useDouble = false;
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
		} else if (!strcasecmp(argv[i], "--output-extra")) {
			outputExtra = true;
		} else if (!strcasecmp(argv[i], "--keep-neg-parallax")) {
			keepNegativeParallax = true;
		} else if (!strcasecmp(argv[i], "--double")) {
			useDouble = true;
		} else {
			showhelp();
			return 0;
		}
	}
	if (!useDouble) {
		ConvertSDSS3<float> convert;
		profile("convert-gaia", convert);
	} else {
		ConvertSDSS3<double> convert;
		profile("convert-gaia", convert);
	}
}
