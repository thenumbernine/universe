/*
TODO
perf test, strtok vs std::getline, buffer all vs explicit unrolled (current) method
*/
#include <cstdlib>		// system()
#include <cstring>		// std::strtok
#include <filesystem>
#include <cmath>
#include <iostream>
#include "batch.h"
#include "util.h"

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
		std::cout 
			<< std::scientific
			<< "magn " << magn
			<< " zeroMagnFlux " << zeroMagnFlux
			<< " wavelength " << wavelength
			<< " restFlux " << restFlux
			<< " dist " << d
			<< std::defaultfloat
			<< std::endl
		;
	}
	return d;
}

void process(std::string const & srcfilename, std::string const & dstfilename) {
	int numEntries = 0;
	int numReadable = 0;

	std::ifstream srcfile(srcfilename);
	if (!srcfile) throw Exception() << "failed to open file " << srcfilename;

	std::ofstream dstfile;
	if (!OMIT_WRITE) {
		dstfile.open(dstfilename, std::ios::binary);
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

	while (!srcfile.eof()) {
		int i = 0;
		double ra, dec;
		double j_m, h_m, k_m, dist_opt;
		double rad_dec, rad_ra;
		double r = 0, rDensity = 0;
		float vtx[3];
		char line[4096];	
		
		getlinen(srcfile, line, sizeof(line));
		
		auto len = std::strlen(line);
		if (len == sizeof(line)-1) throw Exception() << "line buffer overflow";
		
		if (VERBOSE) {
			std::cout << "got length " << len << " line " << line << std::endl;
		}
		numEntries++;
		
		do {
			char *v = std::strtok(line, "|"); if (!v) break;	//ra
			if (VERBOSE) std::cout << "ra: " << v << std::endl; 
			if (!sscanf(v, "%lf", &ra)) break;
			
			v = std::strtok(nullptr, "|"); if (!v) break; //dec
			if (VERBOSE) std::cout << "dec: " << v << std::endl; 
			if (!sscanf(v, "%lf", &dec)) break;

			v = std::strtok(nullptr, "|"); if (!v) break; //err-maj
			if (VERBOSE) std::cout << "err-maj: " << v << std::endl; 
			
			v = std::strtok(nullptr, "|"); if (!v) break; //err-min
			if (VERBOSE) std::cout << "err-min: " << v << std::endl; 
			
			v = std::strtok(nullptr, "|"); if (!v) break; //err-ang
			if (VERBOSE) std::cout << "err-ang: " << v << std::endl; 
			
			v = std::strtok(nullptr, "|"); if (!v) break; //designation
			if (VERBOSE) std::cout << "designation: " << v << std::endl; 

			v = std::strtok(nullptr, "|"); if (!v) break; //j_m
			if (VERBOSE) std::cout << "j_m: " << v << std::endl; 
			if (sscanf(v, "%lf", &j_m)) {
				r += dist(j_m, 1594., 1.235, 3.129e-13);
				rDensity++;
			}
			
			v = std::strtok(nullptr, "|"); if (!v) break; //j_csmig
			if (VERBOSE) std::cout << "j_csmig: " << v << std::endl; 
			
			v = std::strtok(nullptr, "|"); if (!v) break; //j_msigcom
			if (VERBOSE) std::cout << "j_msigcom: " << v << std::endl; 
			
			v = std::strtok(nullptr, "|"); if (!v) break; //j_snr
			if (VERBOSE) std::cout << "j_snr: " << v << std::endl; 
		
			v = std::strtok(nullptr, "|"); if (!v) break; //h_m
			if (VERBOSE) std::cout << "h_m: " << v << std::endl; 
			if (sscanf(v, "%lf", &h_m)) {
				r += dist(h_m, 1024, 1.662, 1.133e-13);
				rDensity++;
			}
			
			v = std::strtok(nullptr, "|"); if (!v) break; //h_csmig
			if (VERBOSE) std::cout << "h_csmig: " << v << std::endl; 
			
			v = std::strtok(nullptr, "|"); if (!v) break; //h_msigcom
			if (VERBOSE) std::cout << "h_msigcom: " << v << std::endl; 
			
			v = std::strtok(nullptr, "|"); if (!v) break; //h_snr
			if (VERBOSE) std::cout << "h_snr: " << v << std::endl; 
		
			v = std::strtok(nullptr, "|"); if (!v) break; //k_m
			if (VERBOSE) std::cout << "k_m: " << v << std::endl; 
			if (sscanf(v, "%lf", &k_m)) {
				r += dist(k_m, 666.7, 2.159, 4.283e-14);
				rDensity++;
			}
	
			//look for dist_opt ...
			int got_dist_opt = 0;
			do {
				v = std::strtok(nullptr, "|"); if (!v) break;	///k_cmsig
				v = std::strtok(nullptr, "|"); if (!v) break;	///k_msigcom
				v = std::strtok(nullptr, "|"); if (!v) break;	///k_snr
				v = std::strtok(nullptr, "|"); if (!v) break;	///ph_qual
				v = std::strtok(nullptr, "|"); if (!v) break;	///rd_flg
				v = std::strtok(nullptr, "|"); if (!v) break;	///bl_flg
				v = std::strtok(nullptr, "|"); if (!v) break;	///cc_flg
				v = std::strtok(nullptr, "|"); if (!v) break;	///ndet
				v = std::strtok(nullptr, "|"); if (!v) break;	///prox
				v = std::strtok(nullptr, "|"); if (!v) break;	///pxpa
				v = std::strtok(nullptr, "|"); if (!v) break;	///pxcntr
				v = std::strtok(nullptr, "|"); if (!v) break;	///gal_contam
				v = std::strtok(nullptr, "|"); if (!v) break;	///mp_flg
				v = std::strtok(nullptr, "|"); if (!v) break;	///pts_key/cntr
				v = std::strtok(nullptr, "|"); if (!v) break;	///hemis
				v = std::strtok(nullptr, "|"); if (!v) break;	///date
				v = std::strtok(nullptr, "|"); if (!v) break;	///scan
				v = std::strtok(nullptr, "|"); if (!v) break;	///glon
				v = std::strtok(nullptr, "|"); if (!v) break;	///glat
				v = std::strtok(nullptr, "|"); if (!v) break;	///x_scan
				v = std::strtok(nullptr, "|"); if (!v) break;	///jdate
				v = std::strtok(nullptr, "|"); if (!v) break;	///j_psfchi
				v = std::strtok(nullptr, "|"); if (!v) break;	///h_psfchi
				v = std::strtok(nullptr, "|"); if (!v) break;	///k_psfchi
				v = std::strtok(nullptr, "|"); if (!v) break;	///j_m_stdap
				v = std::strtok(nullptr, "|"); if (!v) break;	///j_msig_stdap
				v = std::strtok(nullptr, "|"); if (!v) break;	///h_m_stdap
				v = std::strtok(nullptr, "|"); if (!v) break;	///h_msig_stdap
				v = std::strtok(nullptr, "|"); if (!v) break;	///k_m_stdap
				v = std::strtok(nullptr, "|"); if (!v) break;	///k_msig_stdap
				v = std::strtok(nullptr, "|"); if (!v) break;	///dist_edge_ns
				v = std::strtok(nullptr, "|"); if (!v) break;	///dist_edge_ew
				v = std::strtok(nullptr, "|"); if (!v) break;	///dist_edge_flg
				v = std::strtok(nullptr, "|"); if (!v) break;	///dup_src
				v = std::strtok(nullptr, "|"); if (!v) break;	///use_src
				v = std::strtok(nullptr, "|"); if (!v) break;	///a
				v = std::strtok(nullptr, "|"); if (!v) break;	///dist_opt
				got_dist_opt = sscanf(v, "%lf", &dist_opt);
#if 0	
				v = std::strtok(nullptr, "|"); if (!v) break;	///phi_opt
				v = std::strtok(nullptr, "|"); if (!v) break;	///b_m_opt
				v = std::strtok(nullptr, "|"); if (!v) break;	///vr_m_opt
				v = std::strtok(nullptr, "|"); if (!v) break;	///nopt_mchs
				v = std::strtok(nullptr, "|"); if (!v) break;	///ext_key
				v = std::strtok(nullptr, "|"); if (!v) break;	///scan_key
				v = std::strtok(nullptr, "|"); if (!v) break;	///coadd_key
				v = std::strtok(nullptr, "|"); if (!v) break;	///coadd
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
				std::cout << r << "\t" << (1./dist_opt) << std::endl;
			}
			double rad_ra = ra * M_PI / 180.0;
			double rad_dec = dec * M_PI / 180.0;
			double cos_dec = cos(rad_dec);
			vtx[0] = (float)(usingR * cos(rad_ra) * cos_dec);
			vtx[1] = (float)(usingR * sin(rad_ra) * cos_dec);
			vtx[2] = (float)(usingR * sin(rad_dec));
			if (VERBOSE) {
				std::cout << " -- calculated values -- " << std::endl;
				std::cout
					<< "ra " << ra
					<< "dec" << dec
					<< "j_m" << j_m
					<< "h_m" << h_m
					<< "k_m" << k_m
					<< "r" << r
					<< "rDensity" << rDensity	//TODO weight by errors?
					<< "1/dist_opt" << (got_dist_opt ? 1./dist_opt : NAN)
					<< "rad_ra" << rad_ra
					<< "rad_dec" << rad_dec
					<< "x" << vtx[0]
					<< "y" << vtx[1]
					<< "z" << vtx[2]
					<< std::endl
				;
			}
			if (!std::isnan(vtx[0]) && !std::isnan(vtx[1]) && !std::isnan(vtx[2])
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
						std::cout << "writing..." << std::endl;
					}
					dstfile.write(reinterpret_cast<char const *>(vtx), sizeof(vtx));
				}
			}

			if (INTERACTIVE) {
				if (getchar() == 'q') {
					exit(0);	
				}
			}
		} while (0);
	}

	std::cout 
		<< "num entries: " << numEntries
		<< "num readable entries: " << numReadable
		<< "num dist_opts: " << num_dist_opts 
		<< std::endl
	;
#if 0	//now in getstats
	if (numReadable) {
		double ra_stddev = sqrt(ra_sqavg - ra_avg * ra_avg);
		double dec_stddev = sqrt(dec_sqavg - dec_avg * dec_avg);
		std::cout 
			<< "ra min " << ra_min
			<< " max "  << ra_max
			<< " avg "  << ra_avg
			<< " stddev "  << ra_stddev
			<< std::endl
			<< "dec min " << dec_min
			<< " max "  << dec_max
			<< " avg "  << dec_avg
			<< " stddev " << dec_stddev
			<< std::endl
		;
	}
#endif
}

void runOnGZip(const char *basename) {
	std::string dstname = std::string() + "datasets/allsky/points/" + basename + ".f32";

	if (!FORCE && std::filesystem::exists(dstname)) {
		std::cout << "file " << dstname << " already exists" << std::endl;
		return;
	}

	std::string rawname = std::string() + "datasets/allsky/raw/" + basename;

	if (!std::filesystem::exists(rawname)) {
		std::string cmd = std::string() + "7z x -odatasets/allsky/raw datasets/allsky/source/" + basename + ".gz";
		std::cout << "extracting " << cmd << std::endl;
		int code = std::system(cmd.c_str());
		if (code) {
			throw Exception() << cmd << " failed with error code " << code;
		}
	}
	
	//for all files named psc_???.gz
	std::cout << "processing " << rawname << std::endl;;
	process(rawname, dstname);

	if (!PRESERVE) {
		std::cout << "deleting " << rawname << std::endl;
		if (!std::filesystem::remove(rawname)) throw Exception() << "failed to delete file " << rawname;
	}
}

struct ConvertWorker {
	typedef std::string ArgType;
	std::string desc(const std::string &basename) { return std::string() + "file " + basename; }

	ConvertWorker(BatchProcessor<ConvertWorker> *batch_) {} 
	
	void operator()(const std::string &basename) {
		runOnGZip(basename.c_str());
	}
};

void _main(std::vector<std::string> const & args) {
	BatchProcessor<ConvertWorker> batch;
	
	bool gotFile = false, gotDir = false;
	int totalFiles = 0;
	auto h = HandleArgs(args, {
		{"--verbose", {"= shows verbose information.", {[&](){
			VERBOSE = true;
		}}}},
		{"--wait", {"= waits for key at each entry.  implies verbose.", {[&](){
			VERBOSE = true;
			INTERACTIVE = true;
		}}}},
		{"--keep", {"= keep the extracted file after unzipping.", {[&](){
			PRESERVE = true;
		}}}},
		{"--nowrite", {"= do not write f32 file.  useful for verbose.", {[&](){
			OMIT_WRITE = true;
		}}}},
		{"--all", {"= convert all files in the datasets/allsky/source dir.", {[&](){
			gotDir = true;
		}}}},
		{"--file", {"<file>	= convert only this file.  omit path and ext.", {[&](std::string s){
			gotFile = true;
			batch.addThreadArg(s);
			totalFiles++;
		}}}},
		{"--use_dist_opt", {"= use 1/dist_opt for distance.", {[&](){
			USE_DIST_OPT = true;
		}}}},
		{"--r_vs_dist_opt", {"= output comparison of flux r vs dist_opt.", {[&](){
			R_VS_DIST_OPT = true;
		}}}},
		{"--force", {"= run even if the destination file exists.", {[&](){
			FORCE = true;
		}}}},
		{"--threads", {"<n> = specify the number of threads to use.", {std::function<void(int)>([&](int n){
			batch.setNumThreads(n);
		})}}},
	});

	if (!gotFile && !gotDir) {	
		std::cout << "expected a file or a dir" << std::endl;
		h.showhelp();
		return;
	}

	if (gotDir) {
		for (auto const & i : getDirFileNames(std::string() + "datasets/allsky/points")) {
			std::string base, ext;
			getFileNameParts(i, base, ext);
			if (ext == "gz") {
				totalFiles++;
				batch.addThreadArg(base);
			}
		}	
	}

	std::filesystem::create_directory("datasets/allsky/points");
	std::filesystem::create_directory("datasets/allsky/raw");

	double deltaTime = profile("batch", [&](){
		batch();
	});
	std::cout << (deltaTime / (double)totalFiles) << " seconds per file" << std::endl;
}

int main(int argc, char **argv) {
	try {
		_main({argv, argv + argc});
	} catch (std::exception &t) {
		std::cerr << "error: " << t.what() << std::endl;
		return 1;
	}
	return 0;
}
