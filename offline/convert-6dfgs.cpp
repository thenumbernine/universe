#include <stdio.h>
#include <string.h>
#include <math.h>
#include <direct.h>

#include "exception.h"
#include "util.h"

using namespace std;

struct Convert2MRS {
	void operator()() {
		const char *srcfilename = "datasets/6dfgs/source/6dFGSzDR3.txt";
		const char *dstfilename = "datasets/6dfgs/points/points.f32";	
		int numEntries = 0;
		int numReadable = 0;

		mkdir("datasets/6dfgs/points");

		FILE *srcfile = fopen(srcfilename, "r");
		if (!srcfile) throw Exception() << "failed to open file " << srcfilename;

		FILE *dstfile = fopen(dstfilename, "wb");
		if (!dstfile) throw Exception() << "failed to open file " << dstfilename;

		
		while (!feof(srcfile)) {
			double lon, lat, redshift;
			float vtx[3];
			char line[4096];	
			if (!fgets(line, sizeof(line), srcfile)) break;
			if (strlen(line) == sizeof(line)-1) throw Exception() << "line buffer overflow";
			
			if (line[0] == '#') continue;
			numEntries++;

			do {
				char *v = strtok(line, " "); if (!v) break;	//ID
				v = strtok(NULL, " "); if (!v) break; //R.A. hrs
				v = strtok(NULL, " "); if (!v) break; //R.A. min 
				v = strtok(NULL, " "); if (!v) break; //R.A. sec 
				v = strtok(NULL, " "); if (!v) break; //Dec. deg 
				v = strtok(NULL, " "); if (!v) break; //Dec. min 
				v = strtok(NULL, " "); if (!v) break; //Dec. sec 
				v = strtok(NULL, " "); if (!v) break; //# measurements
				v = strtok(NULL, " "); if (!v) break; //# measurements used in final cz
				v = strtok(NULL, " "); if (!v) break; //recalibrated b_J magnitude
				v = strtok(NULL, " "); if (!v) break; //programme ID number
				v = strtok(NULL, " "); if (!v) break; //recalibrated r_F magnitude
				v = strtok(NULL, " "); if (!v) break; //SuperCOSMOS classifier: 1 = galaxy, 2 = star, 3 = unclassifiable, 4 = noise
				v = strtok(NULL, " "); if (!v) break; //sum of comparison flags
				v = strtok(NULL, " "); if (!v) break; //best redshift
				if (!sscanf(v, "%lf", &redshift)) break;
				
				v = strtok(NULL, " "); if (!v) break; //combined uncertainty
				v = strtok(NULL, " "); if (!v) break; //code identifying source of redshift
				v = strtok(NULL, " "); if (!v) break; //best redshift quality value
				
				v = strtok(NULL, " "); if (!v) break; //galactic latitude
				if (!sscanf(v, "%lf", &lat)) break;
				
				v = strtok(NULL, " "); if (!v) break; //galacitic longitude
				if (!sscanf(v, "%lf", &lon)) break;
				
				v = strtok(NULL, " "); if (!v) break; //galactic extinction in V magnitudes
				v = strtok(NULL, " "); if (!v) break; //weight source had for first round
				v = strtok(NULL, " "); if (!v) break; // TARGETID number from the 6dFGS database
				v = strtok(NULL, " "); if (!v) break; //template does
				v = strtok(NULL, " "); if (!v) break; //name of redshift field file
				v = strtok(NULL, " "); if (!v) break; //SPECID

				//printf("%f %f %f\n", lon, lat, redshift);
				//continue;

				//galactic latitude and longitude are in degrees
				double rad_ra = lon * M_PI / 180.0;
				double rad_dec = lat * M_PI / 180.0;
				double cos_dec = cos(rad_dec);
				//redshift is in km/s
				double H0 = 69.32;	//km/s/Mpc
				//H0 *=26.99150576602659;	//ehh, calibratingn for andromeda's distance ... is that andromeda? 
				//distance is in Mpc
				double distance = redshift / H0;
				vtx[0] = (float)(distance * cos(rad_ra) * cos_dec);
				vtx[1] = (float)(distance * sin(rad_ra) * cos_dec);
				vtx[2] = (float)(distance * sin(rad_dec));

				if (!isnan(vtx[0]) && !isnan(vtx[1]) && !isnan(vtx[2])
					&& vtx[0] != INFINITY && vtx[0] != -INFINITY 
					&& vtx[1] != INFINITY && vtx[1] != -INFINITY 
					&& vtx[2] != INFINITY && vtx[2] != -INFINITY
				) {
					numReadable++;
					fwrite(vtx, sizeof(vtx), 1, dstfile);
				}
			} while (0);
		}
		
		fclose(srcfile);
		fclose(dstfile);

		cout << "num entries: " << numEntries << endl;
		cout << "num readable: " << numReadable << endl;
	}
};

int main() {
	Convert2MRS convert;
	profile("convert-2mrs", convert);
}

