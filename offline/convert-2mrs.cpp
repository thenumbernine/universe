/*
usage:
first pass: 	convert-2mrs			generates point file and generate catalog.specs
second pass: 	convert-2mrs --catalog	generates catalog.dat using catalog.specs
*/
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <limits>

#include "exception.h"
#include "util.h"
#include "defs.h"
#include "stat.h"

enum {
	COL_2MASS_ID,
	COL_PHOTO_CONFUSION,
	COL_GALAXY_TYPE,
	COL_SOURCE_OF_TYPE,
	COL_INPUT_CATALOG,
	COL_BIB_CODE,
	COL_GALAXY_NAME,
	NUM_COLS
};

const char *colNames[NUM_COLS] = {
	"_2MASS_ID",
	"photometryConfusionFlags",
	"galaxyType",
	"sourceOfType",
	"inputCatalogCode",
	"bibliographicCode",
	"galaxyName",
};

bool addMilkyWay = false;
bool useRedshiftMinThreshold = false;
double redshiftMinThreshold = -std::numeric_limits<double>::infinity();
bool VERBOSE = false;
bool INTERACTIVE = false;
bool showRanges = false;

struct Convert2MRS {
	bool writingCatalog;
	Convert2MRS() : writingCatalog(false) {}
	void operator()() {
		char line[4096];	
		
		const char *sourceFileName = "datasets/2mrs/source/2mrs_v240/catalog/2mrs_1175_done.dat";
		const char *pointDestFileName = "datasets/2mrs/points/points.f32";	
		const char *catalogDestFileName = "datasets/2mrs/catalog.dat";	
		const char *catalogSpecFileName = "datasets/2mrs/catalog.specs";
		int numEntries = 0;
		int numReadable = 0;

		std::filesystem::create_directory("datasets/2mrs/points");

		//attempt to read the catalog spec file, if it is there
		int colMaxLens[NUM_COLS] = {0};
		
		if (writingCatalog) {
			std::map<std::string, int> specFileMap;
			{
				std::ifstream catalogSpecFile(catalogSpecFileName);
				if (!catalogSpecFile) throw Exception() << "failed to find spec file " << catalogSpecFileName;
				
				for (;;) {
					if (catalogSpecFile.eof()) break; 
					
					getlinen(catalogSpecFile, line, sizeof(line));
					char key[32];
					int value;
					
					char *v = strtok(line, "="); if (!v) throw Exception() << "expected key"; 
					strncpy(key, v, sizeof(key));
					
					v = strtok(NULL, "="); if (!v) throw Exception() << "expected value for key " << key; 
					if (!sscanf(v, "%d", &value)) throw Exception() << "failed to parse line " << line; 
					
					specFileMap[key] = value;
				}
			}

			for (std::map<std::string,int>::iterator i = specFileMap.begin(); i != specFileMap.end(); ++i) {
				std::cout << i->first << " = " << i->second << std::endl;
			}

			for (int j = 0; j < NUM_COLS; j++) {
				std::map<std::string,int>::iterator i = specFileMap.find(colNames[j]);
				if (i == specFileMap.end()) throw Exception() << "expected key " << colNames[j]; 
				colMaxLens[j] = i->second;
			}
		}
			
		Stat statRedshift;
		Stat statDistance;
		Stat statLatitude;
		Stat statLongitude;

		{
			std::ifstream sourceFile(sourceFileName);
			if (!sourceFile) throw Exception() << "failed to open file " << sourceFileName;

			std::ofstream pointDestFile(pointDestFileName, std::ios::binary);
			if (!pointDestFile) throw Exception() << "failed to open file " << pointDestFileName;

			std::ofstream catalogDestFile;
			if (writingCatalog) {	
				catalogDestFile.open(catalogDestFileName, std::ios::binary);	//binary so it is byte-accurate, so i can fseek through it
				if (!catalogDestFile) throw Exception() << "failed to open file " << catalogDestFileName;
			}

			char cols[NUM_COLS][32]; 

			if (addMilkyWay) {
				strncpy(cols[COL_2MASS_ID], "", sizeof(cols[COL_2MASS_ID]));
				*cols[COL_PHOTO_CONFUSION] = 0;
				*cols[COL_GALAXY_TYPE] = 0;
				*cols[COL_SOURCE_OF_TYPE] = 0;
				*cols[COL_INPUT_CATALOG] = 0;
				*cols[COL_BIB_CODE] = 0;
				strncpy(cols[COL_GALAXY_NAME], "Milky Way", sizeof(cols[COL_GALAXY_NAME]));

				float vtx[3] = {0,0,0};
				pointDestFile.write(reinterpret_cast<char*>(vtx), sizeof(vtx));
			
				if (!writingCatalog) {
					for (int j = 0; j < NUM_COLS; j++) {
						int len = strlen(cols[j]);
						if (len > colMaxLens[j]) colMaxLens[j] = len;
					}
				} else {
					for (int j = 0; j < NUM_COLS; j++) {
						catalogDestFile.write(cols[j], colMaxLens[j]);
					}
				}	
			}

			while (!sourceFile.eof()) {
				double lon, lat, redshift;
				double k_c = NAN;
			
				float vtx[3];

				getlinen(sourceFile, line, sizeof(line));
				int len = strlen(line);
				if (len == sizeof(line)-1) throw Exception() << "line buffer overflow";
				if (!len) continue;
				if (line[0] == '#') continue;
				numEntries++;

				do {
					char *v = strtok(line, " "); if (!v) break;	//ID
					strncpy(cols[COL_2MASS_ID], v, sizeof(cols[COL_2MASS_ID]));

					v = strtok(NULL, " "); if (!v) break; //RAdeg
					v = strtok(NULL, " "); if (!v) break; //DECdeg
					v = strtok(NULL, " "); if (!v) break; //l
					if (!sscanf(v, "%lf", &lon)) break;

					v = strtok(NULL, " "); if (!v) break;	//b
					if (!sscanf(v, "%lf", &lat)) break;

					v = strtok(NULL, " "); if (!v) break;	//k_c
					if (!sscanf(v, "%lf", &k_c)) k_c = NAN;
					
					v = strtok(NULL, " "); if (!v) break;	//h_c
					v = strtok(NULL, " "); if (!v) break;	//j_c
					v = strtok(NULL, " "); if (!v) break;	//k_tc
					v = strtok(NULL, " "); if (!v) break;	//h_tc
					v = strtok(NULL, " "); if (!v) break;	//j_tc
					v = strtok(NULL, " "); if (!v) break;	//e_k
					v = strtok(NULL, " "); if (!v) break;	//e_h
					v = strtok(NULL, " "); if (!v) break;	//e_j
					v = strtok(NULL, " "); if (!v) break;	//e_kt
					v = strtok(NULL, " "); if (!v) break;	//e_ht
					v = strtok(NULL, " "); if (!v) break;	//e_jt
					v = strtok(NULL, " "); if (!v) break;	//e_bv
					v = strtok(NULL, " "); if (!v) break;	//r_iso
					v = strtok(NULL, " "); if (!v) break;	//r_ext
					v = strtok(NULL, " "); if (!v) break;	//b/a
					
					v = strtok(NULL, " "); if (!v) break;	//flgs
					strncpy(cols[COL_PHOTO_CONFUSION], v, sizeof(cols[COL_PHOTO_CONFUSION]));
					
					v = strtok(NULL, " "); if (!v) break;	//type
					strncpy(cols[COL_GALAXY_TYPE], v, sizeof(cols[COL_GALAXY_TYPE]));
					
					v = strtok(NULL, " "); if (!v) break;	//ts
					strncpy(cols[COL_SOURCE_OF_TYPE], v, sizeof(cols[COL_SOURCE_OF_TYPE]));
					
					v = strtok(NULL, " "); if (!v) break;	//v
					if (!sscanf(v, "%lf", &redshift)) break;
					
					v = strtok(NULL, " "); if (!v) break;	//e_v
					v = strtok(NULL, " "); if (!v) break;	//c
					strncpy(cols[COL_INPUT_CATALOG], v, sizeof(cols[COL_INPUT_CATALOG]));
					
					v = strtok(NULL, " "); if (!v) break;	//vsrc
					strncpy(cols[COL_BIB_CODE], v, sizeof(cols[COL_BIB_CODE]));
					
					v = strtok(NULL, " "); if (!v) break;	//CAT_ID
					strncpy(cols[COL_GALAXY_NAME], v, sizeof(cols[COL_GALAXY_NAME]));

					if (useRedshiftMinThreshold && redshift < redshiftMinThreshold) continue;

					//galactic latitude and longitude are in degrees
					double rad_ra = lon * M_PI / 180.0;
					double rad_dec = lat * M_PI / 180.0;
					double cos_rad_dec = cos(rad_dec);
					//redshift is in km/s
					//distance is in Mpc
					double distance = redshift / HUBBLE_CONSTANT;
					vtx[0] = (float)(distance * cos(rad_ra) * cos_rad_dec);
					vtx[1] = (float)(distance * sin(rad_ra) * cos_rad_dec);
					vtx[2] = (float)(distance * sin(rad_dec));

					/*
					//2mrs specific paper:
					//http://arxiv.org/pdf/astro-ph/0610005).
					
					const double zeroPointOffset = 0.017;	//+- 0.005
					const double fluxZeroMagn = 1.122e-14;	//+- 1.891e-16 W/cm^2
					galaxyFlux = fluxZeroMagn * pow(10., -.4 * (k_c + zeroPointOffset));
					
					dN = A * pow(z, gamma) * exp(-pow(z/z_c, alpha)) dz
					*/

					/*
					//SDSS3 paper:
					//http://iopscience.iop.org/1367-2630/10/12/125015
					
					double linkingLength = 
					double b = pow(4./3. * M_PI * meanDensityOfGalaxies, 1./3.) * linkingLength;
					double bRadial = 8 * bTangential;

					double cosOmega = 
						cos_rad_decA * cos_rad_decB * (
						cos_rad_raA * cos_rad_raB + sin_rad_raA * sin_rad_raB) 
						+ sin_rad_decA * sin_rad_decB;
					
					//trig-simplified, yet with one additional trig evaluation ...
					//double cosOmega = cos_rad_decA * cos_rad_decB * cos(rad_raA - rad_raB) + sin_rad_decA * sin_rad_decB;

					if (cosOmega < 1.) cosOmega = 1.;
					if (cosOmega > 1.) cosOmega = 1.;
					double omega = acos(cosOmega);
					double distRadial = (distA + distB) * sin(omega);
					double distTangential = fabs(distA + distB);
					if (distRadial < bRadial * pow(meanDensityOfGalaxies, -1./3.);
					&& distTangential < bTangential) 
					{
						//A and B are grouped 
						return true;
					}
					*/

					if (!isnan(vtx[0]) && !isnan(vtx[1]) && !isnan(vtx[2])
						&& vtx[0] != INFINITY && vtx[0] != -INFINITY 
						&& vtx[1] != INFINITY && vtx[1] != -INFINITY 
						&& vtx[2] != INFINITY && vtx[2] != -INFINITY
					) {
						numReadable++;	
						
						if (showRanges) {
							statRedshift.accum(redshift, numReadable);
							statDistance.accum(distance, numReadable);
							statLatitude.accum(lat, numReadable);
							statLongitude.accum(lon, numReadable);
						}

					
						pointDestFile.write(reinterpret_cast<char*>(vtx), sizeof(vtx));
					
						if (!writingCatalog) {
							for (int j = 0; j < NUM_COLS; j++) {
								int collen = strlen(cols[j]);
								if (collen > colMaxLens[j]) colMaxLens[j] = collen;
							}
						} else {
							for (int j = 0; j < NUM_COLS; j++) {
								catalogDestFile.write(cols[j], colMaxLens[j]);
							}
						}
					}
					
					if (VERBOSE) {
						std::cout << line << std::endl;
						std::cout << "lon " << lon << std::endl; 
						std::cout << "lat " << lat << std::endl; 
						std::cout << "redshift (km/s) " << redshift << std::endl; 
						std::cout << "redshift (z) " << (redshift / SPEED_OF_LIGHT) << std::endl; 
						std::cout << "K20 " << k_c << std::endl; 
					}

					if (INTERACTIVE) {
						if (getchar() == 'q') {
							exit(0);	
						}
					}
				} while (0);
			}
		}

		std::cout << "num entries: " << numEntries << std::endl;
		std::cout << "num readable: " << numReadable << std::endl;

		if (!writingCatalog) {
			std::ofstream catalogSpecFile(catalogSpecFileName);
			if (!catalogSpecFile) throw Exception() << "failed to open file " << catalogSpecFileName;
			for (int j = 0; j < NUM_COLS; j++) {
				catalogSpecFile << colNames[j] << "=" << colMaxLens[j] << std::endl;
			}
		}
	
		if (showRanges) {
			std::cout 
				<< statRedshift.rw("redshift") << std::endl
				<< statDistance.rw("distance") << std::endl
				<< statLatitude.rw("latitude") << std::endl
				<< statLongitude.rw("longitude") << std::endl
			;
		}
	}
};

void showhelp() {
	std::cout
	<< "usage: convert-2mrs <options>" << std::endl
	<< "options:" << std::endl
	<< "    --verbose            output values" << std::endl
	<< "    --show-ranges        show ranges of certain fields" << std::endl
	<< "    --wait               wait for keypress after each entry.  'q' stops" << std::endl
	<< "    --catalog            use the datasets/2mrs/catalog.spec file " << std::endl
	<< "                         to generate datasets/2mrs/catalog.dat" << std::endl
	<< "    --min-redshift <cz>  specify minimum redshift" << std::endl
	<< "    --add-milky-way      artificially add the milky way" << std::endl
	;
}

int main(int argc, char **argv) {
	std::vector<std::string> args(argv, argv + argc);
	
	Convert2MRS convert;
	for (int i = 1; i < args.size(); i++) {
		if (args[i] == "--help") {
			showhelp();
			return 0;
		} else if (args[i] == "--verbose") {
			VERBOSE = true;
		} else if (args[i] == "--show-ranges") {
			showRanges = true;
		} else if (args[i] == "--wait") {
			INTERACTIVE = true;
		} else if (args[i] == "--catalog") {
			convert.writingCatalog = true;
		} else if (args[i] == "--min-redshift" && i < args.size()-1) {
			useRedshiftMinThreshold = true;
			redshiftMinThreshold = atof(argv[++i]);
		} else if (args[i] == "--add-milky-way") {
			addMilkyWay = true;
		} else {
			showhelp();
			return 0;
		}
	}
	profile("convert-2mrs", [&](){
		convert();
	});
}

