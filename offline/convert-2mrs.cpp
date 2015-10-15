/*
usage:
first pass: 	convert-2mrs			generates point file and generate catalog.specs
second pass: 	convert-2mrs --catalog	generates catalog.dat using catalog.specs
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#include <map>
#include <string>
#include <limits>

#include "exception.h"
#include "util.h"
#include "defs.h"

using namespace std;

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
bool useMinRedshift = false;
double minRedshift = -numeric_limits<double>::infinity();
bool VERBOSE = false;
bool INTERACTIVE = false;

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

		mkdir("datasets/2mrs/points", 0777);

		//attempt to read the catalog spec file, if it is there
		int colMaxLens[NUM_COLS] = {0};
		
		if (writingCatalog) {
			FILE *catalogSpecFile = fopen(catalogSpecFileName, "r");
			if (!catalogSpecFile) throw Exception() << "failed to find spec file " << catalogSpecFileName;
			map<string, int> specFileMap;
			
			for (;;) {
				if (feof(catalogSpecFile)) break; 
				if (!fgets(line, sizeof(line), catalogSpecFile)) break;
				char key[32];
				int value;
				
				char *v = strtok(line, "="); if (!v) throw Exception() << "expected key"; 
				strncpy(key, v, sizeof(key));
				
				v = strtok(NULL, "="); if (!v) throw Exception() << "expected value for key " << key; 
				if (!sscanf(v, "%d", &value)) throw Exception() << "failed to parse line " << line; 
				
				specFileMap[key] = value;
			}
			fclose(catalogSpecFile);

			for (map<string,int>::iterator i = specFileMap.begin(); i != specFileMap.end(); ++i) {
				cout << i->first << " = " << i->second << endl;
			}

			for (int j = 0; j < NUM_COLS; j++) {
				map<string,int>::iterator i = specFileMap.find(colNames[j]);
				if (i == specFileMap.end()) throw Exception() << "expected key " << colNames[j]; 
				colMaxLens[j] = i->second;
			}
		}

		FILE *sourceFile = fopen(sourceFileName, "r");
		if (!sourceFile) throw Exception() << "failed to open file " << sourceFileName;

		FILE *pointDestFile = fopen(pointDestFileName, "wb");
		if (!pointDestFile) throw Exception() << "failed to open file " << pointDestFileName;

		FILE *catalogDestFile = NULL;
		if (writingCatalog) {	
			catalogDestFile = fopen(catalogDestFileName, "wb");	//binary so it is byte-accurate, so i can fseek through it
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
			fwrite(vtx, sizeof(vtx), 1, pointDestFile);
		
			if (!writingCatalog) {
				for (int j = 0; j < NUM_COLS; j++) {
					int len = strlen(cols[j]);
					if (len > colMaxLens[j]) colMaxLens[j] = len;
				}
			} else {
				for (int j = 0; j < NUM_COLS; j++) {
					fwrite(cols[j], colMaxLens[j], 1, catalogDestFile);
				}
			}	
		}

		while (!feof(sourceFile)) {
			double lon, lat, redshift;
			double k_c = NAN;
		
			float vtx[3];
			int len;

			if (!fgets(line, sizeof(line), sourceFile)) break;
			if (strlen(line) == sizeof(line)-1) throw Exception() << "line buffer overflow";
			
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

				if (useMinRedshift && redshift < minRedshift) continue;

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
					fwrite(vtx, sizeof(vtx), 1, pointDestFile);
				
					if (!writingCatalog) {
						for (int j = 0; j < NUM_COLS; j++) {
							int len = strlen(cols[j]);
							if (len > colMaxLens[j]) colMaxLens[j] = len;
						}
					} else {
						for (int j = 0; j < NUM_COLS; j++) {
							fwrite(cols[j], colMaxLens[j], 1, catalogDestFile);
						}
					}
				}
				
				if (VERBOSE) {
					cout << line << endl;
					cout << "lon " << lon << endl; 
					cout << "lat " << lat << endl; 
					cout << "redshift (km/s) " << redshift << endl; 
					cout << "redshift (z) " << (redshift / SPEED_OF_LIGHT) << endl; 
					cout << "K20 " << k_c << endl; 
				}

				if (INTERACTIVE) {
					if (getchar() == 'q') {
						exit(0);	
					}
				}
			} while (0);
		}
	
		if (writingCatalog) {
			fclose(catalogDestFile);
		}
		fclose(sourceFile);
		fclose(pointDestFile);

		cout << "num entries: " << numEntries << endl;
		cout << "num readable: " << numReadable << endl;

		if (!writingCatalog) {
			FILE *catalogSpecFile = fopen(catalogSpecFileName, "w");
			if (!catalogSpecFile) throw Exception() << "failed to open file " << catalogSpecFileName;
			for (int j = 0; j < NUM_COLS; j++) {
				fprintf(catalogSpecFile, "%s=%d\n", colNames[j], colMaxLens[j]);
			}
			fclose(catalogSpecFile);
		}
	}
};

void showhelp() {
	cout
	<< "usage: convert-2mrs <options>" << endl
	<< "options:" << endl
	<< "	--verbose	output values" << endl
	<< "	--wait		wait for keypress after each entry.  'q' stops" << endl
	<< "	--catalog 	use the datasets/2mrs/catalog.spec file " << endl
	<< "				to generate datasets/2mrs/catalog.dat" << endl
	<< "	--min-redshift <cz> 	specify minimum redshift" << endl
	<< "	--add-milky-way			artificially add the milky way" << endl
	;
}

int main(int argc, char **argv) {
	Convert2MRS convert;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--help")) {
			showhelp();
			return 0;
		} else if (!strcmp(argv[i], "--verbose")) {
			VERBOSE = true;
		} else if (!strcmp(argv[i], "--wait")) {
			INTERACTIVE = true;
		} else if (!strcmp(argv[i], "--catalog")) {
			convert.writingCatalog = true;
		} else if (!strcmp(argv[i], "--min-redshift") && i < argc-1) {
			useMinRedshift = true;
			minRedshift = atof(argv[++i]);
		} else if (!strcmp(argv[i], "--add-milky-way")) {
			addMilkyWay = true;
		} else {
			showhelp();
			return 0;
		}
	}
	profile("convert-2mrs", convert);
}

