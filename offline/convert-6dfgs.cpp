#include <cstring>
#include <cmath>		//std::isnan
#include <filesystem>
#include "exception.h"
#include "util.h"

struct Convert6DFGS {
	void operator()() {
		const char *srcfilename = "datasets/6dfgs/source/6dFGSzDR3.txt";
		const char *dstfilename = "datasets/6dfgs/points/points.f32";	
		int numEntries = 0;
		int numReadable = 0;

		std::filesystem::create_directory("datasets/6dfgs/points");

		std::ifstream srcfile(srcfilename);
		if (!srcfile) throw Exception() << "failed to open file " << srcfilename;

		std::ofstream dstfile(dstfilename, std::ios::binary);
		if (!dstfile) throw Exception() << "failed to open file " << dstfilename;
		
		while (!srcfile.eof()) {
			double lon, lat, redshift;
			float vtx[3];
			char line[4096];	
			getlinen(srcfile, line, sizeof(line));
			int const len = strlen(line);
			if (!len) continue;
			if (len == sizeof(line)-1) throw Exception() << "line buffer overflow";
			if (line[0] == '#') continue;
			numEntries++;

			do {
				char *v = strtok(line, " "); if (!v) break;	//ID
				v = strtok(nullptr, " "); if (!v) break; //R.A. hrs
				v = strtok(nullptr, " "); if (!v) break; //R.A. min 
				v = strtok(nullptr, " "); if (!v) break; //R.A. sec 
				v = strtok(nullptr, " "); if (!v) break; //Dec. deg 
				v = strtok(nullptr, " "); if (!v) break; //Dec. min 
				v = strtok(nullptr, " "); if (!v) break; //Dec. sec 
				v = strtok(nullptr, " "); if (!v) break; //# measurements
				v = strtok(nullptr, " "); if (!v) break; //# measurements used in final cz
				v = strtok(nullptr, " "); if (!v) break; //recalibrated b_J magnitude
				v = strtok(nullptr, " "); if (!v) break; //programme ID number
				v = strtok(nullptr, " "); if (!v) break; //recalibrated r_F magnitude
				v = strtok(nullptr, " "); if (!v) break; //SuperCOSMOS classifier: 1 = galaxy, 2 = star, 3 = unclassifiable, 4 = noise
				v = strtok(nullptr, " "); if (!v) break; //sum of comparison flags
				v = strtok(nullptr, " "); if (!v) break; //best redshift
				if (!sscanf(v, "%lf", &redshift)) break;
				
				v = strtok(nullptr, " "); if (!v) break; //combined uncertainty
				v = strtok(nullptr, " "); if (!v) break; //code identifying source of redshift
				v = strtok(nullptr, " "); if (!v) break; //best redshift quality value
				
				v = strtok(nullptr, " "); if (!v) break; //galactic latitude
				if (!sscanf(v, "%lf", &lat)) break;
				
				v = strtok(nullptr, " "); if (!v) break; //galacitic longitude
				if (!sscanf(v, "%lf", &lon)) break;
				
				v = strtok(nullptr, " "); if (!v) break; //galactic extinction in V magnitudes
				v = strtok(nullptr, " "); if (!v) break; //weight source had for first round
				v = strtok(nullptr, " "); if (!v) break; // TARGETID number from the 6dFGS database
				v = strtok(nullptr, " "); if (!v) break; //template does
				v = strtok(nullptr, " "); if (!v) break; //name of redshift field file
				v = strtok(nullptr, " "); if (!v) break; //SPECID

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

				if (!std::isnan(vtx[0]) && !std::isnan(vtx[1]) && !std::isnan(vtx[2])
					&& vtx[0] != INFINITY && vtx[0] != -INFINITY 
					&& vtx[1] != INFINITY && vtx[1] != -INFINITY 
					&& vtx[2] != INFINITY && vtx[2] != -INFINITY
				) {
					numReadable++;
					dstfile.write(reinterpret_cast<char const *>(vtx), sizeof(vtx));
				}
			} while (0);
		}

		std::cout << "num entries: " << numEntries << std::endl;
		std::cout << "num readable: " << numReadable << std::endl;
	}
};

int main() {
	profile("convert-6dfgs", [](){
		Convert6DFGS convert;
		convert();
	});
}
