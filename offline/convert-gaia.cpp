/*
usage:
convert-gaia			generates point file
*/
#include <cstring>
#include <filesystem>
#include <vector>
#include <map>
#include <string>
#include <sstream>
#include <limits>
#include "stat.h"
#include "exception.h"
#include "util.h"
#include "fits-util.h"
#include "defs.h"

bool verbose = false;
bool getColumns = false;
bool interactive = false;
bool omitWrite = false;
bool showRanges = false;
bool outputExtra = false;
bool keepNegativeParallax = false;

double parsec_in_meters = 30856780000000000;
double year_in_seconds = 31557600; 

template<typename OutputPrecision>
struct ConvertSDSS {
	auto getOutputExt() const {
		if constexpr (std::is_same_v<OutputPrecision, float>) {
			return "f32";
		} else if constexpr (std::is_same_v<OutputPrecision, double>) {
			return "f64";
		} else {
			static_assert("don't have an extension for this type");
		}
	}
	
	ConvertSDSS() {}
	
	void operator()() {
		// well these should already be here if we're reading from datasets/gaia/source ...
		// unless you want to automate the download steps as well?
		//mkdir("datasets", 0775);
		//mkdir("datasets/gaia", 0775);
		std::filesystem::create_directory("datasets/gaia/points");

		std::string pointDestFileName = std::string() + "datasets/gaia/points/points" + (outputExtra ? "-9col" : "") + "." + getOutputExt();

		std::ofstream pointDestFile;
		if (!omitWrite) {
			pointDestFile.open(pointDestFileName);
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
		
		Stat stat_x, stat_y, stat_z;
		Stat stat_vx, stat_vy, stat_vz;
		
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
			fitsfile *file = NULL;

			FITS_SAFE(fits_open_table(&file, sourceFileName.c_str(), READONLY, &status));
		
			long numRows = 0;
			FITS_SAFE(fits_get_num_rows(file, &numRows, &status));
			
			int numCols = 0;
			FITS_SAFE(fits_get_num_cols(file, &numCols, &status));

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


			/*
			what do we want from the archive?
			cx,cy,cz direction
			z redshift <=> velocity <=> distance
			brightness? color? shape?
			catalog name? NGC_*** MS_*** or whatever other identifier?
			*/
			std::vector<FITSColumn*> columns;
		
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
			//radial velocity (km/s)
			FITSTypedColumn<double> *col_radial_velocity = nullptr; if (outputExtra) { col_radial_velocity = new FITSTypedColumn<double>(file, "radial_velocity"); columns.push_back(col_radial_velocity); }
			//stellar effective temperature (K)
			FITSTypedColumn<float> *col_teff_val = nullptr; if (outputExtra) { col_teff_val = new FITSTypedColumn<float>(file, "teff_val"); columns.push_back(col_teff_val); }
			//stellar radius (solar radii)
			FITSTypedColumn<float> *col_radius_val = nullptr; if (outputExtra) { col_radius_val = new FITSTypedColumn<float>(file, "radius_val"); columns.push_back(col_radius_val); }
			//stellar luminosity (solar luminosity)
			FITSTypedColumn<float> *col_lum_val = nullptr; if (outputExtra) { col_lum_val = new FITSTypedColumn<float>(file, "lum_val"); columns.push_back(col_lum_val); }

			//well that's 9 columns.  I wasn't storing radius in the HYG data

			if (verbose) {
				for (std::vector<FITSColumn*>::iterator i = columns.begin(); i != columns.end(); ++i) {
					std::cout << " col num " << (*i)->colName << " = " << (*i)->colNum << std::endl;
				}
			}	

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
			
				time_t thistime = time(NULL);
				if (!interactive && thistime != lasttime) {
					lasttime = thistime;
					double frac = (double)rowNum / (double)numRows;
					updatePercent(100. * frac);
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
					std::cout << std::endl;
					std::cout << "source_id = " << value_source_id << std::endl;
					std::cout << "ra = " << value_ra << std::endl;
					std::cout << "dec = " << value_dec << std::endl;
					std::cout << "parallax = " << value_parallax << std::endl;
					if (outputExtra) {
						std::cout << "pmra = " << value_pmra << std::endl;
						std::cout << "pmdec = " << value_pmdec << std::endl;
						std::cout << "radial_velocity = " << value_radial_velocity << std::endl;
						std::cout << "teff_val = " << value_teff_val << std::endl;
						std::cout << "radius_val = " << value_radius_val << std::endl;
						std::cout << "lum_val = " << value_lum_val << std::endl;
					}
				}
			
				//distance from parallax
				double arcsec_parallax = value_parallax * 1e-3;	//convert from milliarcseconds to arcseconds
				double distance = 1./arcsec_parallax;	//convert from arcseconds to parsecs 
				// comment this for universe visualizer
				//distance *= 1e-3;	//parsec to Kpc
				distance *= 1e-6;	//parsec to Mpc

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
						double
							e_ra_x = -sin_ra,
							e_ra_y = cos_ra,
							e_ra_z = 0.,
							
							e_dec_x = -sin_dec * cos_ra,
							e_dec_y = -sin_dec * sin_ra,
							e_dec_z = cos_dec,
							
							e_r_x = cos_dec * cos_ra,
							e_r_y = cos_dec * sin_ra,
							e_r_z = sin_dec;

    					const double AU_YRKMS = 4.740470446;
					
						// http://www.star.bris.ac.uk/~mbt/topcat/sun253/Gaia.html
						/*	
						arcsecond = 1/3,600 degree 
						milliarcsecond = 1/3,600,000 degree = pi/180 1/3,600,000 raidian (unitless)
						milliarcsecond/year = 1/3,600,000 pi/180 radian/year
						*/	
						double pmra_arcsec_year = value_pmra * .001;	//mas/year (milliarcseconds) to arcseconds/year
						double pmra_km_s = distance * pmra_arcsec_year * AU_YRKMS;  //arcseconds/year to km/s
						
						double pmdec_arcsec_year = value_pmdec * .001;	//mas/year (milliarcseconds) to arcseconds/year
						double pmdec_km_s = distance * pmdec_arcsec_year * AU_YRKMS;

						double radial_velocity_km_s = value_radial_velocity;
						
						velocity[0] = (e_ra_x * pmra_km_s + e_dec_x * pmdec_km_s + e_r_x * radial_velocity_km_s) * (1000. / parsec_in_meters * year_in_seconds);
						velocity[1] = (e_ra_y * pmra_km_s + e_dec_y * pmdec_km_s + e_r_y * radial_velocity_km_s) * (1000. / parsec_in_meters * year_in_seconds);
						velocity[2] = (e_ra_z * pmra_km_s + e_dec_z * pmdec_km_s + e_r_z * radial_velocity_km_s) * (1000. / parsec_in_meters * year_in_seconds);
					
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
						
							stat_x.accum(position[0], numReadable);
							stat_y.accum(position[1], numReadable);
							stat_z.accum(position[2], numReadable);
							
							if (outputExtra) {
								stat_pmra.accum(value_pmra, numReadable);
								stat_pmdec.accum(value_pmdec, numReadable);
								stat_radial_velocity.accum(value_radial_velocity, numReadable);
								stat_teff_val.accum(value_teff_val, numReadable);
								stat_radius_val.accum(value_radius_val, numReadable);
								stat_lum_val.accum(value_lum_val, numReadable);
							
								stat_vx.accum(velocity[0], numReadable);
								stat_vy.accum(velocity[1], numReadable);
								stat_vz.accum(velocity[2], numReadable);
							}
						}
						
						if (!omitWrite) {
							pointDestFile.write(reinterpret_cast<char const *>(position), sizeof(position));
							if (outputExtra) {
								pointDestFile.write(reinterpret_cast<char const *>(velocity), sizeof(velocity));
								pointDestFile.write(reinterpret_cast<char const *>(&radius), sizeof(radius));
								pointDestFile.write(reinterpret_cast<char const *>(&temp), sizeof(temp));
								pointDestFile.write(reinterpret_cast<char const *>(&luminosity), sizeof(luminosity));
							}
						}
					}
				}

				if (verbose) {
					std::cout << "distance (Mpc) " << distance << std::endl;
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
			
			FITS_SAFE(fits_close_file(file, &status));
		}

		std::cout << "num readable: " << numReadable << std::endl;
	
		if (showRanges) {
			std::cout 
				<< stat_ra.rw("ra") << std::endl
				<< stat_dec.rw("dec") << std::endl
				<< stat_parallax.rw("parallax") << std::endl
				<< stat_pmra.rw("pmra") << std::endl
				<< stat_pmdec.rw("pmdec") << std::endl
				<< stat_radial_velocity.rw("radial_velocity") << std::endl
				<< stat_teff_val.rw("teff_val") << std::endl
				<< stat_radius_val.rw("radius_val") << std::endl
				<< stat_lum_val.rw("lum_val") << std::endl
				<< stat_distance.rw("distance") << std::endl
				<< stat_x.rw("x") << std::endl
				<< stat_y.rw("y") << std::endl
				<< stat_z.rw("z") << std::endl
				<< stat_vx.rw("vx") << std::endl
				<< stat_vy.rw("vy") << std::endl
				<< stat_vz.rw("vz") << std::endl
			;
		}

		std::cout << "source_id range " << source_id_min << " to " << source_id_max << std::endl;
	}
};

void showhelp() {
	std::cout
	<< "usage: convert-gaia <options>" << std::endl
	<< "options:" << std::endl
	<< "	--verbose				output values" << std::endl
	<< "	--show-ranges			show ranges of certain fields" << std::endl
	<< "	--wait					wait for keypress after each entry.  'q' stops" << std::endl
	<< "	--get-columns			print all column names" << std::endl
	<< "	--nowrite				don't write results.  useful with --verbose or --read-desc" << std::endl
	<< "	--output-extra			also output velocity, temperature, and luminosity" << std::endl
	<< "	--keep-neg-parallax		keep negative parallax" << std::endl
	<< "	--double				output as double precision (default single)" << std::endl
	;
}

int main(int argc, char **argv) {
	bool useDouble = false;
	for (int i = 1; i < argc; i++) {
		if (!std::strcmp(argv[i], "--help")) {
			showhelp();
			return 0;
		} else if (!std::strcmp(argv[i], "--verbose")) {
			verbose = true;
		} else if (!std::strcmp(argv[i], "--show-ranges")) {
			showRanges = true;
		} else if (!std::strcmp(argv[i], "--wait")) {
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
		profile("convert-gaia", [&](){ 
			ConvertSDSS<float> convert;
			convert();
		});
	} else {
		profile("convert-gaia", [&](){ 
			ConvertSDSS<double> convert;
			convert(); 
		});
	}
}
