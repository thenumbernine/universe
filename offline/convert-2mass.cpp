/*
TODO
perf test, strtok vs std::getline, buffer all vs explicit unrolled (current) method
*/
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <iostream>
#include "batch.h"
#include "util.h"

using namespace std;

bool FORCE = false;
bool PRESERVE = false;
bool VERBOSE = false;
bool INTERACTIVE = false;
bool OMIT_WRITE = false;
bool USE_DIST_OPT = false;
bool R_VS_DIST_OPT = false;

/*
http://www.ipac.caltech.edu/2mass/releases/allsky/doc/sec4_5a.html#stardiscrim
"The ability to separate real extended sources (e.g., galaxies, nebulae, H II regions, etc.) from the vastly more numerous stars detected by 2MASS is what fundamentally limits the reliability of any extended source catalog."
-- first sentence, biggest problem, sounds about right.

where'd I get this?
10^(1 - (andromeda magn - vega magn) / 5)

d = sqrt(1 / fluxRatio) * 10^(1 - (mA - M0) / 5)
d sqrt(fluxRatio) = 10^(1 - (mA - M0) / 5)
log10(d sqrt(fluxRatio)) = 1 - (mA - M0) / 5
M0 = 5 * log10(d sqrt(fluxRatio)) - 1 + mA 
M0 = 5 * log10(d) + 2.5 * log10(fluxRatio) - 1 + mA 

L = sigma A T^4, for A = area, sigma = constant, T = temperature <- Stefan-Boltzmann equation:
F = L / A, for F = flux density, A = area of illumination
F = L / (4 pi r^2), for illuminated object with radius 'r' 


or just calibrate it with the distance and bands of the following: http://en.wikipedia.org/wiki/2MASS_0036%2B1821

TODO
http://iopscience.iop.org/1538-3881/135/5/1738/fulltext/aj_135_5_1738.text.html
*/
#define DISTANCE_SCALE	0.038618670083148
//#define DISTANCE_SCALE	2.0796966871037		//<- based on vega's to andromeda's visible magnitude

/*
band:	wavelength	bandwidth	0-magnitude flux 	rest flux
		(m)			(m)			(Jy)				W / m^3
J		1.235e-6	.162e-6		1594				3.129e-23
H		1.662e-6	.251e-6		1024				1.133e-23
Ks		2.159e-6	.262e-6		666.7				4.283e-24

Jy = Jansky = 10^-26 W s / m^2


-- current equation:
flux = 0-magnitude flux Jy * 10^(-.4 * magn) * 3 * 10^-16 / (wavelength microns)^2 
flux = 0-magnitude flux Jy * 10^(-.4 * magn) * 3 * 10^-28 / (wavelength m)^2
flux = 0-magnitude flux (10^-26 W s / m^2) * 3 * 10^-28 * 10^(-.4 * magn) / (wavelength m)^2
flux = 0-magnitude flux (10^-26 W s / m^2) * 3 * 10^-28 * 10^(-.4 * magn) / (wavelength m)^2
flux = 0-magnitude flux-in-Jy * 3 * 10^-54 * 10^(-.4 * magn) / wavelength-in-m^2 * W s / m^4

-- wikipedia on magnitude:
difference in magnitude & difference in intensity (brightness):
ma - mb = -2.5 log10 (Fa / Fb)
(ma - mb) / -2.5 = log10 (Fa / Fb)
10^((ma - mb) / -2.5) = Fa / Fb

-- http://www.astrophysicsspectator.com/topics/observation/MagnitudesAndColors.html
ma - mb = -2.5 log10 (Fa / Fb)
... object j-band magnitude - reference j-band magnitude = -2.5 log10(object flux? / rest flux?)

-- http://ircamera.as.arizona.edu/astr_250/Lectures/Lec13_sml.htm
flux observed = luminosity / (4 pi d^2) 
d = sqrt(luminosity / (4 pi flux observed))
m2 = object-j-magnitude, m1 = 0, flux2 = object-measured-j-flux, flux1 = 0-magnitude-j-flux
object-j-magnitude = -2.5 log10(object-measured-j-flux / 0-magnitude-j-flux)
10^(object-measured-j-magn / -2.5) * 0-magnitude-j-flux (Jy) = object-measured-j-flux (Jy)		<- so magnitude is unitless?
object-measured-j-flux = 10^(-object-measured-j-magn / 2.5) * 0-magnitude-j-flux

object-measured-j-magn = object-j-luminosity / (4 pi object-distance^2)
10^(-object-measured-j-magn / 2.5) * 0-magnitude-j-flux = object-j-luminosity / (4 pi object-distance^2)
object-j-luminosity / (10^(-object-measured-j-magn / 2.5) * 0-magnitude-j-flux) = 4 pi object-distance^2
object-j-luminosity * 10^(object-measured-j-magn / 2.5) / 0-magnitude-j-flux = 4 pi object-distance^2
object-j-luminosity * 10^(object-measured-j-magn / 2.5) / (4 pi 0-magnitude-j-flux) = object-distance^2
object-distance = sqrt(object-j-luminosity) * sqrt(10^(object-measured-j-magn / 2.5)) / sqrt(4 pi 0-magnitude-j-flux)
object-distance = 10^(object-measured-j-magn / 5) * sqrt(object-j-luminosity / (4 pi 0-magnitude-j-flux))

so how do we calculate luminosity? (assuming constant-luminosity of all universes)
well ... andromeda has magnitude 3.44 and distance 778154.33743382 pc
vega has magnitude 0 and distance 7.6803649143882 pc
so andromeda j-band magn - vega j-band magn = -2.5 log10 (andromeda j-band flux / vega j-band flux)
	44 = -2.5 log10 (andromeda flux / 1594)
	so we need to either know the j-band flux of andromeda ... or the 


observed-magnitude - 0-magnitude = -2.5 * log10 (observed-flux / 0-magnitude-flux)
(observed-magnitude - 0-magnitude) / -2.5 = log10 (observed-flux / 0-magnitude-flux)
10^((observed-magnitude - 0-magnitude) / -2.5) = observed-flux / 0-magnitude-flux
observed-flux = 0-magnitude-flux * 10^((observed-magnitude - 0-magnitude) / -2.5)

...

-- http://astro.berkeley.edu/badgrads/theses/mliu.ps.gz


*/
double dist(double magn, double zeroMagnFlux, double wavelength, double restFlux) {
	double flux = zeroMagnFlux * pow(10., -.4 * magn) * 3e-16 / (wavelength * wavelength);
	double fluxRatio = flux / restFlux;
	double d = sqrt(1. / fluxRatio);
	d *= DISTANCE_SCALE;
	if (VERBOSE) {
		printf("magn %e zeroMagnFlux %e wavelength %e restFlux %e dist %e\n", magn, zeroMagnFlux, wavelength, restFlux, d);
	}
	return d;
}

void process(const char *srcfilename, const char *dstfilename) {
	int numEntries = 0;
	int numReadable = 0;

	FILE *srcfile = fopen(srcfilename, "r");
	if (!srcfile) throw Exception() << "failed to open file " << srcfilename;

	FILE *dstfile = NULL;
	if (!OMIT_WRITE) {
		dstfile = fopen(dstfilename, "wb");
		if (!dstfile) throw Exception() << "failed to open file " << dstfilename;
	}

	int num_dist_opts = 0;
	double ra_avg = 0;
	double ra_sqavg = 0;
	double ra_min = INFINITY;
	double ra_max = -INFINITY;
	double dec_avg = 0;
	double dec_sqavg = 0;
	double dec_min = INFINITY;
	double dec_max = -INFINITY;

	while (!feof(srcfile)) {
		int i = 0;
		double ra, dec;
		double j_m, h_m, k_m, dist_opt;
		double rad_dec, rad_ra;
		double r = 0,rDensity = 0;
		float vtx[3];
		char line[4096];	
		if (!fgets(line, sizeof(line), srcfile)) break;
		if (strlen(line) == sizeof(line)-1) throw Exception() << "line buffer overflow";
		
		if (VERBOSE) {
			printf("got length %d line %s\n", strlen(line), line);
		}
		numEntries++;
		
		do {
			char *v = strtok(line, "|"); if (!v) break;	//ra
			if (VERBOSE) cout << "ra: " << v << endl; 
			if (!sscanf(v, "%lf", &ra)) break;
			
			v = strtok(NULL, "|"); if (!v) break; //dec
			if (VERBOSE) cout << "dec: " << v << endl; 
			if (!sscanf(v, "%lf", &dec)) break;

			v = strtok(NULL, "|"); if (!v) break; //err-maj
			if (VERBOSE) cout << "err-maj: " << v << endl; 
			
			v = strtok(NULL, "|"); if (!v) break; //err-min
			if (VERBOSE) cout << "err-min: " << v << endl; 
			
			v = strtok(NULL, "|"); if (!v) break; //err-ang
			if (VERBOSE) cout << "err-ang: " << v << endl; 
			
			v = strtok(NULL, "|"); if (!v) break; //designation
			if (VERBOSE) cout << "designation: " << v << endl; 

			v = strtok(NULL, "|"); if (!v) break; //j_m
			if (VERBOSE) cout << "j_m: " << v << endl; 
			if (sscanf(v, "%lf", &j_m)) {
				r += dist(j_m, 1594., 1.235, 3.129e-13);
				rDensity++;
			}
			
			v = strtok(NULL, "|"); if (!v) break; //j_csmig
			if (VERBOSE) cout << "j_csmig: " << v << endl; 
			
			v = strtok(NULL, "|"); if (!v) break; //j_msigcom
			if (VERBOSE) cout << "j_msigcom: " << v << endl; 
			
			v = strtok(NULL, "|"); if (!v) break; //j_snr
			if (VERBOSE) cout << "j_snr: " << v << endl; 
		
			v = strtok(NULL, "|"); if (!v) break; //h_m
			if (VERBOSE) cout << "h_m: " << v << endl; 
			if (sscanf(v, "%lf", &h_m)) {
				r += dist(h_m, 1024, 1.662, 1.133e-13);
				rDensity++;
			}
			
			v = strtok(NULL, "|"); if (!v) break; //h_csmig
			if (VERBOSE) cout << "h_csmig: " << v << endl; 
			
			v = strtok(NULL, "|"); if (!v) break; //h_msigcom
			if (VERBOSE) cout << "h_msigcom: " << v << endl; 
			
			v = strtok(NULL, "|"); if (!v) break; //h_snr
			if (VERBOSE) cout << "h_snr: " << v << endl; 
		
			v = strtok(NULL, "|"); if (!v) break; //k_m
			if (VERBOSE) cout << "k_m: " << v << endl; 
			if (sscanf(v, "%lf", &k_m)) {
				r += dist(k_m, 666.7, 2.159, 4.283e-14);
				rDensity++;
			}
	
			//look for dist_opt ...
			int got_dist_opt = 0;
			do {
				v = strtok(NULL, "|"); if (!v) break;	///k_cmsig
				v = strtok(NULL, "|"); if (!v) break;	///k_msigcom
				v = strtok(NULL, "|"); if (!v) break;	///k_snr
				v = strtok(NULL, "|"); if (!v) break;	///ph_qual
				v = strtok(NULL, "|"); if (!v) break;	///rd_flg
				v = strtok(NULL, "|"); if (!v) break;	///bl_flg
				v = strtok(NULL, "|"); if (!v) break;	///cc_flg
				v = strtok(NULL, "|"); if (!v) break;	///ndet
				v = strtok(NULL, "|"); if (!v) break;	///prox
				v = strtok(NULL, "|"); if (!v) break;	///pxpa
				v = strtok(NULL, "|"); if (!v) break;	///pxcntr
				v = strtok(NULL, "|"); if (!v) break;	///gal_contam
				v = strtok(NULL, "|"); if (!v) break;	///mp_flg
				v = strtok(NULL, "|"); if (!v) break;	///pts_key/cntr
				v = strtok(NULL, "|"); if (!v) break;	///hemis
				v = strtok(NULL, "|"); if (!v) break;	///date
				v = strtok(NULL, "|"); if (!v) break;	///scan
				v = strtok(NULL, "|"); if (!v) break;	///glon
				v = strtok(NULL, "|"); if (!v) break;	///glat
				v = strtok(NULL, "|"); if (!v) break;	///x_scan
				v = strtok(NULL, "|"); if (!v) break;	///jdate
				v = strtok(NULL, "|"); if (!v) break;	///j_psfchi
				v = strtok(NULL, "|"); if (!v) break;	///h_psfchi
				v = strtok(NULL, "|"); if (!v) break;	///k_psfchi
				v = strtok(NULL, "|"); if (!v) break;	///j_m_stdap
				v = strtok(NULL, "|"); if (!v) break;	///j_msig_stdap
				v = strtok(NULL, "|"); if (!v) break;	///h_m_stdap
				v = strtok(NULL, "|"); if (!v) break;	///h_msig_stdap
				v = strtok(NULL, "|"); if (!v) break;	///k_m_stdap
				v = strtok(NULL, "|"); if (!v) break;	///k_msig_stdap
				v = strtok(NULL, "|"); if (!v) break;	///dist_edge_ns
				v = strtok(NULL, "|"); if (!v) break;	///dist_edge_ew
				v = strtok(NULL, "|"); if (!v) break;	///dist_edge_flg
				v = strtok(NULL, "|"); if (!v) break;	///dup_src
				v = strtok(NULL, "|"); if (!v) break;	///use_src
				v = strtok(NULL, "|"); if (!v) break;	///a
				v = strtok(NULL, "|"); if (!v) break;	///dist_opt
				got_dist_opt = sscanf(v, "%lf", &dist_opt);
#if 0	
				v = strtok(NULL, "|"); if (!v) break;	///phi_opt
				v = strtok(NULL, "|"); if (!v) break;	///b_m_opt
				v = strtok(NULL, "|"); if (!v) break;	///vr_m_opt
				v = strtok(NULL, "|"); if (!v) break;	///nopt_mchs
				v = strtok(NULL, "|"); if (!v) break;	///ext_key
				v = strtok(NULL, "|"); if (!v) break;	///scan_key
				v = strtok(NULL, "|"); if (!v) break;	///coadd_key
				v = strtok(NULL, "|"); if (!v) break;	///coadd
#endif
			} while (0);
			if (got_dist_opt) num_dist_opts++;

			double usingR;
			if (USE_DIST_OPT) {
				if (!got_dist_opt) break;
				usingR = 1. / dist_opt;	
			} else {
				if (!rDensity) break;
				usingR = r / rDensity;
			}
			
			if (R_VS_DIST_OPT && got_dist_opt) {
				printf("%f\t%f\n", r, 1./dist_opt);
			}
			double rad_ra = ra * M_PI / 180.0;
			double rad_dec = dec * M_PI / 180.0;
			double cos_dec = cos(rad_dec);
			vtx[0] = (float)(usingR * cos(rad_ra) * cos_dec);
			vtx[1] = (float)(usingR * sin(rad_ra) * cos_dec);
			vtx[2] = (float)(usingR * sin(rad_dec));
			if (VERBOSE) {
				cout << " -- calculated values -- " << endl;
				printf("ra %f\n", ra);
				printf("dec %f\n", dec);
				printf("j_m %f\n", j_m);
				printf("h_m %f\n", h_m);
				printf("k_m %f\n", k_m);
				printf("r %f\n", r);
				printf("rDensity %f\n", rDensity);	//TODO weight by errors?
				printf("1/dist_opt %f\n", got_dist_opt ? 1./dist_opt : (0./0.));
				printf("rad_ra %f\n", rad_ra);
				printf("rad_dec %f\n", rad_dec);
				printf("x %f\n", vtx[0]);
				printf("y %f\n", vtx[1]);
				printf("z %f\n", vtx[2]);
				printf("\n");
			}
			if (!isnan(vtx[0]) && !isnan(vtx[1]) && !isnan(vtx[2])
				&& vtx[0] != INFINITY && vtx[0] != -INFINITY 
				&& vtx[1] != INFINITY && vtx[1] != -INFINITY 
				&& vtx[2] != INFINITY && vtx[2] != -INFINITY
			) {
				//inc before use in moving avg
				numReadable++;
				
				if (ra < ra_min) ra_min = ra;
				if (ra > ra_max) ra_max = ra;
				ra_avg += (ra - ra_avg) / (double)numReadable;
				ra_sqavg += (ra*ra - ra_sqavg) / (double)numReadable;
				if (dec < dec_min) dec_min = dec;
				if (dec > dec_max) dec_max = dec;
				dec_avg += (dec - dec_avg) / (double)numReadable;
				dec_sqavg += (dec*dec - dec_sqavg) / (double)numReadable;
				
				if (!OMIT_WRITE) {
					if (VERBOSE) {
						printf("writing...\n");
					}
					fwrite(vtx, sizeof(vtx), 1, dstfile);
				}
			}

			if (INTERACTIVE) {
				if (getchar() == 'q') {
					exit(0);	
				}
			}
		} while (0);
	}

	fclose(srcfile);
	if (!OMIT_WRITE) {
		fclose(dstfile);
	}

	printf("num entries: %d\n", numEntries);
	printf("num readable entries: %d\n", numReadable);

	printf("num dist_opts: %d\n", num_dist_opts);
#if 0	//now in getstats
	if (numReadable) {
		double ra_stddev = sqrt(ra_sqavg - ra_avg * ra_avg);
		double dec_stddev = sqrt(dec_sqavg - dec_avg * dec_avg);
		printf("ra min %f max %f avg %f stddev %f\n", ra_min, ra_max, ra_avg, ra_stddev);
		printf("dec min %f max %f avg %f\n", dec_min, dec_max, dec_avg, dec_stddev);
	}
#endif
}

void runOnGZip(const char *basename) {
	char dstname[FILENAME_MAX];
	char rawname[FILENAME_MAX];

	snprintf(dstname, sizeof(dstname), "datasets/allsky/points/%s.f32", basename);
	if (!FORCE && fileexists(dstname)) {
		printf("file %s already exists\n", dstname);
		return;
	}

	snprintf(rawname, sizeof(rawname), "datasets/allsky/raw/%s", basename);

	if (!fileexists(rawname)) {
		char cmd[FILENAME_MAX];
		snprintf(cmd, sizeof(cmd), "7z x -odatasets/allsky/raw datasets/allsky/source/%s.gz", basename);
		printf("extracting %s\n", basename);
		system(cmd);
	}
	
	//for all files named psc_???.gz
	printf("processing %s\n", rawname);
	process(rawname, dstname);

	if (!PRESERVE) {
		printf("deleting %s\n", rawname);
		if (remove(rawname) == -1) throw Exception() << "failed to delete file " << rawname;
	}
}

struct ConvertWorker {
	typedef string ArgType;
	string desc(const string &basename) { return string() + "file " + basename; }

	ConvertWorker(BatchProcessor<ConvertWorker> *batch_) {} 
	
	void operator()(const string &basename) {
		runOnGZip(basename.c_str());
	}
};


void showhelp(void) {
	cout
	<< "usage: convert <options>" << endl
	<< "options:" << endl
	<< "	--verbose	shows verbose information." << endl
	<< "	--wait		waits for key at each entry.  implies verbose." << endl
	<< "	--keep		keep the extracted file after unzipping." << endl
	<< "	--nowrite	do not write f32 file.  useful for verbose." << endl
	<< "	--all		convert all files in the datasets/allsky/source dir." << endl
	<< "	--use_dist_opt	use 1/dist_opt for distance." << endl
	<< "	--r_vs_dist_opt	output comparison of flux r vs dist_opt." << endl
	<< "	--force		run even if the destination file exists." << endl 
	<< "	--file <file>	convert only this file.  omit path and ext." << endl
	<< "	--threads <n>	specify the number of threads to use." << endl
	;
}

int main(int argc, char **argv) {
	BatchProcessor<ConvertWorker> batch;
	
	bool gotFile = false, gotDir = false;
	int totalFiles = 0;
	
	for (int k = 1; k < argc; k++) {
		if (!strcmp(argv[k], "--verbose")) {
			VERBOSE = true;
		} else if (!strcmp(argv[k], "--wait")) {
			VERBOSE = true;
			INTERACTIVE = true;
		} else if (!strcmp(argv[k], "--keep")) {
			PRESERVE = true;
		} else if (!strcmp(argv[k], "--nowrite")) {
			OMIT_WRITE = true;
		} else if (!strcmp(argv[k], "--use_dist_opt")) {
			USE_DIST_OPT = true;
		} else if (!strcmp(argv[k], "--r_vs_dist_opt")) {
			R_VS_DIST_OPT = true;
		} else if (!strcmp(argv[k], "--force")) { 
			FORCE = true;
		} else if (!strcmp(argv[k], "--all")) {
			gotDir = true;
			list<string> dirFilenames;
			for (list<string>::iterator i = dirFilenames.begin(); i != dirFilenames.end(); ++i) {
				string base, ext;
				getFileNameParts(*i, base, ext);
				if (ext == "gz") {
					totalFiles++;
					batch.addThreadArg(base);
				}
			}
		} else if (!strcmp(argv[k], "--file") && k < argc-1) {
			gotFile = true;
			totalFiles++;
			batch.addThreadArg(argv[++k]);
		} else if (!strcmp(argv[k], "--threads") && k < argc-1) {
			batch.setNumThreads(atoi(argv[++k]));
		} else {
			showhelp();
			return 0;
		}
	}

	if (!gotFile && !gotDir) {	
		showhelp();
		return 0;
	}

	mkdir("datasets/allsky/points", 0777);
	mkdir("datasets/allsky/raw", 0777);

	double deltaTime = profile("batch", batch);
	cout << (deltaTime / (double)totalFiles) << " seconds per file" << endl;
}

