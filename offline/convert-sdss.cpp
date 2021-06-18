/*
usage:
convert-sdss			generates point file
*/
#include <filesystem>
#include <vector>
#include <map>
#include <string>
#include <limits>
#include "stat.h"
#include "exception.h"
#include "util.h"
#include "fits-util.h"
#include "defs.h"

bool verbose = false;
bool spherical = false;
bool getColumns = false;
bool interactive = false;

//2008 SubbaRao et al: "the radial extent of the survey is restricted to the redshift range 0.01 < z < 0.11"
bool useMinRedshift = false;

bool readStringDescs = false;
bool trackStrings = false;
bool omitWrite = false;
bool showRanges = false;

//notice: "Visualization of large scale structure from the Sloan Digital Sky Survey" by M U SubbaRao, M A Aragón-Calvo, H W Chen, J M Quashnock, A S Szalay and D G York in New Journal of Physics
// samples redshift from 0.01 < z < 0.11
double minRedshift = -std::numeric_limits<double>::infinity();

struct IFITSTrackBehavior {
	virtual void track(int rowNum) = 0;
	virtual void printAll() = 0;
};

template<typename PARENT>
struct FITSTrackBehavior : public PARENT, public IFITSTrackBehavior {
	std::map<typename PARENT::CTYPE, int> valueSet;
	FITSTrackBehavior(fitsfile *file_, const char *colName_) : PARENT(file_, colName_) {}

	void track(int rowNum) {
		++valueSet[PARENT::read(rowNum)];
	}

	void printAll() {
		std::cout << "values of " << PARENT::colName << ":" << std::endl;
		for (std::map<std::string, int>::iterator i = valueSet.begin(); i != valueSet.end(); ++i) {
			std::cout << i->first << " : " << i->second << std::endl;
		}
		std::cout << std::endl;
	}
};

typedef FITSTrackBehavior<FITSStringColumn> FITSStringTrackColumn;

struct ConvertSDSS3 {
	ConvertSDSS3() {}
	void operator()() {
		const char *sourceFileName = "datasets/sdss/source/specObj-dr16.fits";
		const char *pointDestFileName = spherical
			? "datasets/sdss/points/spherical.f64"
			: "datasets/sdss/points/points.f32";

		std::filesystem::create_directory("datasets/sdss/points");

		fitsfile *file = nullptr;
		fitsSafe(fits_open_table, &file, sourceFileName, READONLY);
	
		long numRows = 0;
		fitsSafe(fits_get_num_rows, file, &numRows);
		
		int numCols = 0;
		fitsSafe(fits_get_num_cols, file, &numCols);

		std::cout << "numCols: " << numCols << std::endl;
		std::cout << "numRows: " << numRows << std::endl;

		if (getColumns || verbose) {
			int status = 0;
			for (;;) {
				int colNum = 0;
				char colName[256];
				memset(colName, 0, sizeof(colName));
				fits_get_colname(file, CASESEN, (char *)"*", colName, &colNum, &status);
				if (status == COL_NOT_FOUND) break;
				if (status != 0 && status != COL_NOT_UNIQUE) throw Exception() << fitsGetError(status);
				std::cout << colNum << "\t" << colName << std::endl;
			}
			std::cout << std::endl;
			if (getColumns) return;
		}
		
		std::ofstream pointDestFile;
		if (!omitWrite) {
			pointDestFile.open(pointDestFileName);
			if (!pointDestFile) throw Exception() << "failed to open file " << pointDestFileName << " for writing";
		}

		/*
		what do we want from the archive?
		cx,cy,cz direction
		z redshift <=> velocity <=> distance
		brightness? color? shape?
		catalog name? NGC_*** MS_*** or whatever other identifier?
		*/
		std::vector<std::shared_ptr<FITSColumn>> columns;
		std::vector<std::shared_ptr<IFITSTrackBehavior>> trackColumns;
	
		int readStringStartIndex = columns.size();
			
		if (readStringDescs) {		//catalog stuff?
			columns.push_back(std::make_shared<FITSStringColumn>(file, "SURVEY"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "INSTRUMENT"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "CHUNK"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "PROGRAMNAME"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "PLATERUN"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "PLATEQUALITY"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "PLATESN2"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "DEREDSN2"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "LAMBDA_EFF"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "BLUEFIBER"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "ZOFFSET"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "SNTURNOFF"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "NTURNOFF"));
			columns.push_back(std::make_shared<FITSTypedColumn<bool>>(file, "SPECPRIMARY"));
			columns.push_back(std::make_shared<FITSTypedColumn<bool>>(file, "SPECSDSS"));
			columns.push_back(std::make_shared<FITSTypedColumn<bool>>(file, "SPECLEGACY"));
			columns.push_back(std::make_shared<FITSTypedColumn<bool>>(file, "SPECSEGUE"));
			columns.push_back(std::make_shared<FITSTypedColumn<bool>>(file, "SPECSEGUE1"));
			columns.push_back(std::make_shared<FITSTypedColumn<bool>>(file, "SPECSEGUE2"));
			columns.push_back(std::make_shared<FITSTypedColumn<bool>>(file, "SPECBOSS"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "BOSS_SPECOBJ_ID"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "SPECOBJID"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "FLUXOBJID"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "BESTOBJID"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "TARGETOBJID"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "PLATEID"));
			columns.push_back(std::make_shared<FITSTypedColumn<short>>(file, "NSPECOBS"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "FIRSTRELEASE"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "RUN2D"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "RUN1D"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "DESIGNID"));
		}
	
		//cartesian
		auto col_CX = std::make_shared<FITSTypedColumn<double>>(file, "CX"); columns.push_back(col_CX);
		auto col_CY = std::make_shared<FITSTypedColumn<double>>(file, "CY"); columns.push_back(col_CY);
		auto col_CZ = std::make_shared<FITSTypedColumn<double>>(file, "CZ"); columns.push_back(col_CZ);
	
		//spherical
		auto col_PLUG_RA = std::make_shared<FITSTypedColumn<double>>(file, "PLUG_RA"); columns.push_back(col_PLUG_RA);
		auto col_PLUG_DEC = std::make_shared<FITSTypedColumn<double>>(file, "PLUG_DEC"); columns.push_back(col_PLUG_DEC);

		if (readStringDescs) {
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "XFOCAL"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "YFOCAL"));
		}
		auto col_SOURCETYPE = std::make_shared<FITSStringTrackColumn>(file, "SOURCETYPE"); columns.push_back(col_SOURCETYPE);
		auto col_TARGETTYPE = std::make_shared<FITSStringTrackColumn>(file, "TARGETTYPE"); columns.push_back(col_TARGETTYPE);
		if (readStringDescs) {
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "PRIMTARGET"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "SECTARGET"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "LEGACY_TARGET1"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "LEGACY_TARGET2"));
			columns.push_back(std::make_shared<FITSTypedColumn<long long>>(file, "SPECIAL_TARGET1"));
			columns.push_back(std::make_shared<FITSTypedColumn<long long>>(file, "SPECIAL_TARGET2"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "SEGUE1_TARGET1"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "SEGUE1_TARGET2"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "SEGUE2_TARGET1"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "SEGUE2_TARGET2"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "MARVELS_TARGET1"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "MARVELS_TARGET2"));
			columns.push_back(std::make_shared<FITSTypedColumn<long long>>(file, "BOSS_TARGET1"));
			columns.push_back(std::make_shared<FITSTypedColumn<long long>>(file, "BOSS_TARGET2"));
			columns.push_back(std::make_shared<FITSTypedColumn<long long>>(file, "ANCILLARY_TARGET1"));
			columns.push_back(std::make_shared<FITSTypedColumn<long long>>(file, "ANCILLARY_TARGET2"));
			columns.push_back(std::make_shared<FITSTypedColumn<short>>(file, "SPECTROGRAPHID"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "PLATE"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "TILE"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "MJD"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "FIBERID"));
			//columns.push_back(std::make_shared<FITSTypedColumn<int[5]>>(file, "OBJID"));
			columns.push_back(std::make_shared<FITSTypedColumn<double>>(file, "PLUG_RA"));
			columns.push_back(std::make_shared<FITSTypedColumn<double>>(file, "PLUG_DEC"));
		}
		auto col_CLASS = std::make_shared<FITSStringTrackColumn>(file, "CLASS"); columns.push_back(col_CLASS);
		auto col_SUBCLASS = std::make_shared<FITSStringTrackColumn>(file, "SUBCLASS"); columns.push_back(col_SUBCLASS);
		auto col_Z = std::make_shared<FITSTypedColumn<float>>(file, "Z"); columns.push_back(col_Z);
		if (readStringDescs) {
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "Z_ERR"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "RCHI2"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "DOF"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "RCHI2DIFF"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "TFILE"));
			//columns.push_back(std::make_shared<FITSTypedColumn<float[10]>>(file, "TCOLUMN"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "NPOLY"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "TFILE"));
			//columns.push_back(std::make_shared<FITSTypedColumn<float[10]>>(file, "THETA"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "VDISP"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "VDISP_ERR"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "VDISPZ"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "VDISPZ_ERR"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "VDISPDOF"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "VDISPCHI2"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "VDISPNPIX"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "WAVEMIN"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "WAVEMAX"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "WCOVERAGE"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "ZWARNING"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "SN_MEDIAN_ALL"));
			//columns.push_back(std::make_shared<FITSTypedColumn<float[5]>>(file, "SN_MEDIAN"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "CHI68P"));
			//columns.push_back(std::make_shared<FITSTypedColumn<float[10]>>(file, "FRACNSIGMA"));
			//columns.push_back(std::make_shared<FITSTypedColumn<float[10]>>(file, "FRACNSIGHI"));
			//columns.push_back(std::make_shared<FITSTypedColumn<float[10]>>(file, "FRACNSIGLO"));
			//columns.push_back(std::make_shared<FITSTypedColumn<float[5]>>(file, "SPECTROFLUX"));
			//columns.push_back(std::make_shared<FITSTypedColumn<float[5]>>(file, "SPECTROFLUX_IVAR"));
			//columns.push_back(std::make_shared<FITSTypedColumn<float[5]>>(file, "SPECTROSYNFLUX"));
			//columns.push_back(std::make_shared<FITSTypedColumn<float[5]>>(file, "SPECTROSYNFLUX_IVAR"));
			//columns.push_back(std::make_shared<FITSTypedColumn<float[5]>>(file, "SPECTROSKYFLUX"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "ANYANDMASK"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "ANYORMASK"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "SPEC1_G"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "SPEC1_R"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "SPEC1_I"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "SPEC2_G"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "SPEC2_R"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "SPEC2_I"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "ELODIE_FILENAME"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "ELODIE_OBJECT"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "ELODIE_SPTYPE"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "ELODIE_BV"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "ELODIE_TEFF"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "ELODIE_LOGG"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "ELODIE_FEH"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "ELODIE_Z"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "ELODIE_Z_ERR"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "ELODIE_Z_MODELERR"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "ELODIE_RCHI2"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "ELODIE_DOF"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "Z_NOQSO"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "Z_ERR_NOQSO"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "ZWARNING_NOQSO"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "CLASS_NOQSO"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "SUBCLASS_NOQSO"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "RCHI2DIFF_NOQSO"));
			columns.push_back(std::make_shared<FITSTypedColumn<float>>(file, "Z_PERSON"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "CLASS_PERSON"));
			columns.push_back(std::make_shared<FITSTypedColumn<int>>(file, "Z_CONF_PERSON"));
			columns.push_back(std::make_shared<FITSStringColumn>(file, "COMMENTS_PERSON"));
			//columns.push_back(std::make_shared<FITSTypedColumn<float[5]>>(file, "CALIBFLUX"));
			//columns.push_back(std::make_shared<FITSTypedColumn<float[5]>>(file, "CALIBFLUX_IVAR"));
		}

		if (trackStrings) {
			trackColumns.push_back(col_SOURCETYPE);
			trackColumns.push_back(col_TARGETTYPE);
			trackColumns.push_back(col_CLASS);
			trackColumns.push_back(col_SUBCLASS);
		}

		if (verbose) {
			for (std::vector<std::shared_ptr<FITSColumn>>::iterator i = columns.begin(); i != columns.end(); ++i) {
				std::cout << " col num " << (*i)->colName << " = " << (*i)->colNum << std::endl;
			}
		}	

		//non spherical
		Stat stat_cx, stat_cy, stat_cz;
		
		//spherical
		Stat stat_ra, stat_dec;
		
		Stat stat_z;

		//TODO assert columns are of the type  matching what I will be reading

		int numReadable = 0;
		
		//now cycle through rows and pick out values that we want
		if (!interactive) {
			std::cout << "processing     ";
			std::cout.flush();
		}
		
		auto const updatePercent = [&](int const percent) {
			std::cout << "\b\b\b\b" 
				<< std::setw(3)
				<< percent 
				<< std::setw(0)
				<< "%";
			std::cout.flush();
		};

		time_t lasttime = -1;
		for (int rowNum = 1; rowNum <= numRows; ++rowNum) {
		
			time_t thistime = time(nullptr);
			if (!interactive && thistime != lasttime) {
				lasttime = thistime;
				double frac = (double)rowNum / (double)numRows;
				updatePercent(100. * frac);
			}

			for (std::vector<std::shared_ptr<IFITSTrackBehavior>>::iterator i = trackColumns.begin(); i != trackColumns.end(); ++i) {
				(*i)->track(rowNum);
			}
				
			if (readStringDescs ) {	//catalog stuff
				for (std::vector<std::shared_ptr<FITSColumn>>::iterator i = columns.begin() + readStringStartIndex; i != columns.end(); ++i) {
					std::cout << (*i)->colName << " = " << (*i)->readStr(rowNum) << std::endl;
				}
			}

			std::string value_CLASS = col_CLASS->read(rowNum);
			//galaxies only for now
			if (value_CLASS != "GALAXY") continue;

			double value_Z = col_Z->read(rowNum);
			if (useMinRedshift && value_Z < minRedshift) continue;
			
			if (!spherical) {
				double value_CX = col_CX->read(rowNum);
				double value_CY = col_CY->read(rowNum);
				double value_CZ = col_CZ->read(rowNum);
				//TODO also use value_OBJTYPE & col_OBJTYPE?
	
				if (verbose) {
					std::cout << "CX = " << value_CX << std::endl;
					std::cout << "CY = " << value_CY << std::endl;
					std::cout << "CZ = " << value_CZ << std::endl;
					std::cout << "Z = " << value_Z << std::endl;
				}
			
				double redshift = SPEED_OF_LIGHT * value_Z;
				//redshift is in km/s
				//distance is in Mpc
				double distance = redshift / HUBBLE_CONSTANT;
				float vtx[3];
				vtx[0] = (float)(distance * value_CX); 
				vtx[1] = (float)(distance * value_CY); 
				vtx[2] = (float)(distance * value_CZ);

				if (!isnan(vtx[0]) && !isnan(vtx[1]) && !isnan(vtx[2])
					&& vtx[0] != INFINITY && vtx[0] != -INFINITY 
					&& vtx[1] != INFINITY && vtx[1] != -INFINITY 
					&& vtx[2] != INFINITY && vtx[2] != -INFINITY
				) {
					numReadable++;	
					
					if (showRanges) {
						stat_z.accum(value_Z, numReadable);
						stat_cx.accum(value_CX, numReadable);
						stat_cy.accum(value_CY, numReadable);
						stat_cz.accum(value_CZ, numReadable);
					}
					
					if (!omitWrite) {
						pointDestFile.write(reinterpret_cast<char const *>(vtx), sizeof(vtx));
					}
				}
				if (verbose) {
					std::cout << "distance (Mpc) " << distance << std::endl;
				}
			} else { //spherical
				double value_RA = col_PLUG_RA->read(rowNum);
				double value_DEC = col_PLUG_DEC->read(rowNum);
			
				if (verbose) {
					std::cout << "RA = " << value_RA << std::endl;
					std::cout << "DEC = " << value_DEC << std::endl;
					std::cout << "Z = " << value_Z << std::endl;
				}
		
				double vtx[3];
				vtx[0] = value_Z;
				vtx[1] = value_RA;
				vtx[2] = value_DEC;
				
				if (!isnan(vtx[0]) && !isnan(vtx[1]) && !isnan(vtx[2])
					&& vtx[0] != INFINITY && vtx[0] != -INFINITY 
					&& vtx[1] != INFINITY && vtx[1] != -INFINITY 
					&& vtx[2] != INFINITY && vtx[2] != -INFINITY
				) {
					numReadable++;	
					
					if (showRanges) {
						stat_z.accum(value_Z, numReadable);
						stat_ra.accum(value_RA, numReadable);
						stat_dec.accum(value_DEC, numReadable);
					}
					
					if (!omitWrite) {
						pointDestFile.write(reinterpret_cast<char const *>(vtx), sizeof(vtx));
					}
				}	
			}

			if (verbose) {
				std::cout << "redshift (z) " << value_Z << std::endl;
			}

			if (interactive) {
				if (getchar() == 'q') {
					exit(0);
				}
			}
		}
		if (!interactive) {
			updatePercent(100);
			std::cout << std::endl;
		}

		for (std::vector<std::shared_ptr<IFITSTrackBehavior>>::iterator i = trackColumns.begin(); i != trackColumns.end(); ++i) {
			(*i)->printAll();
		}

		fitsSafe(fits_close_file, file);
		
		std::cout << "num readable: " << numReadable << std::endl;
	
		if (showRanges) {
			std::cout 
				<< stat_z.rw("z") << std::endl
			;
			if (!spherical) {
				std::cout 
					<< stat_cx.rw("cx") << std::endl
					<< stat_cy.rw("cy") << std::endl
					<< stat_cz.rw("cz") << std::endl
				;
			} else {
				std::cout
					<< stat_ra.rw("ra") << std::endl
					<< stat_dec.rw("dec") << std::endl
				;
			}
		}

	}
};

void showhelp() {
	std::cout
		<< "usage: convert-sdss <options>" << std::endl
		<< "options:" << std::endl
		<< "    --verbose               output values" << std::endl
		<< "    --wait                  wait for keypress after each entry.  'q' stops" << std::endl
		<< "    --show-ranges           show ranges of certain fields" << std::endl
		<< "    --read-desc             reads string descriptions" << std::endl
		<< "    --min-redshift <cz>     specify minimum redshift" << std::endl
		<< "    --enum-class            enumerate all classes" << std::endl
		<< "    --get-columns           print all column names" << std::endl
		<< "    --nowrite               don't write results.  useful with --verbose or --read-desc" << std::endl
		<< "    --spherical             output spherical coordinates: z, ra, dec (default outputs xyz)" << std::endl
	;
}

int main(int argc, char **argv) {
	std::vector<std::string> args;
	std::copy(argv, argv+argc, std::back_inserter<std::vector<std::string>>(args));
	
	ConvertSDSS3 convert;
	for (int i = 1; i < args.size(); i++) {
		if (argv[i] == "--help") {
			showhelp();
			return 0;
		} else if (argv[i] == "--verbose") {
			verbose = true;
		} else if (argv[i] == "--spherical") {
			spherical = true;
		} else if (argv[i] == "--wait") {
			verbose = true;
			interactive = true;
		} else if (argv[i] == "--min-redshift" && i < args.size()-1) {
			useMinRedshift = true;
			minRedshift = atof(args[++i].c_str());
		} else if (argv[i] == "--show-ranges") {
			showRanges = true;
		} else if (argv[i] == "--read-desc") {
			readStringDescs  = true;
		} else if (argv[i] == "--enum-class") {
			trackStrings = true;
		} else if (argv[i] == "--get-columns") {
			getColumns = true;
		} else if (argv[i] == "--nowrite") {
			omitWrite = true;
		} else {
			showhelp();
			return 0;
		}
	}
	profile("convert-sdss", convert);
}
