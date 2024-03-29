/*
distance 
=> times Hubble constant => velocity 
=> divide speed of light => redshift

group by the following:


naive approach:
for all clusters
	for all points in this cluster
		for all other clusters
			for all other points in the other cluster 
				calc distance between points
				if it is within the tolerance then
					merge the other cluster into this cluster

for 50k we can associate pointers with points ... but what about with 500m? 
 this might involve keeping an aux set of data alongside the octree: the cluster hash # 
  and for deeper octree leafs (those smaller than the search distance), entire leaves will belong to the same cluster
  so for that it would be best to RLE the cluster hash std::list files


- RLE the cluster hash std::list
- store cluster hashes in a parallel fileset to the octree
- assume no file means RLE'd 0's all through
- and assume cluster hash 0 means "unique hash all to myself"

... now the next challenge is, how do we "iterate through a cluster" ?



*/
#include <iostream>
#include <fstream>
#include <cstring>

#include "octree.h"
#include "defs.h"
#include "util.h"
#include "exception.h"
#include "stat.h"

struct Cluster {
	std::vector<vec3f*> vtxs;
};


//used by method #2 in the paper Angel cites
double avgDensityForRadius[5];
double maxAvgDensityDist = 0;

//used by method #1, from Angel's paper
struct MergeTest_Angle {
	double radialThreshold;//bRadial * pow(meanDensityOfGalaxies, -1./3.);
	double transverseThreshold;//radialThreshold/8.
	Stat angleDifference;
	Stat redshiftDifference;
	
	MergeTest_Angle()
	: radialThreshold(8.),
		transverseThreshold(1.)
	{
	
/*
meanNumGalaxies = nBar_gal(zBar), which is the mean number of galaxies "as a function of the pair redshift"
 so what is the "pair redshift" ?  the pair's redshift added together? or some other function based on their redshift?

n_thr = b^-3 nBar_gal = nBar_gal / b^3 = 1 / (b^3 / nBar_gal) = 1/(b/nBar_gal^1/3)^3 = (b nBar_gal^-1/3)^-3
n_thr^-1/3 = b nBar_gal^-1/3

for b = (4/3 pi nBar_gal r_link^3)^1/3 = (4/3 pi nBar_gal)^1/3 r_link
*/

	}

	bool handleArg(int argc, char **argv, int &k) {
		if (!std::strcmp(argv[k], "--radial") && k < argc-1) {
			radialThreshold = atof(argv[++k]);
		} else if (!std::strcmp(argv[k], "--transverse") && k < argc-1) {
			transverseThreshold = atof(argv[++k]);
		} else {
			return false;
		}
		return true;
	}

	bool operator()(const vec3f &a, const vec3f &b) {
		//radial distance in Mpc
		double distA = a.len();	
		double distB = b.len();
		//redshift (z) = distance Mpc * Hubble constant km/s/Mpc / speed of light km/s
		//double velA = distA * HUBBLE_CONSTANT / SPEED_OF_LIGHT;
		//double velB = distB * HUBBLE_CONSTANT / SPEED_OF_LIGHT;
		//redshift (z)
		vec3d unitA = a * (1. / distA);
		vec3d unitB = b * (1. / distB);
		double cosOmega = vec3d::dot(unitA, unitB);
		if (cosOmega < -1.) cosOmega = -1.;
		if (cosOmega > 1.) cosOmega = 1.;
		double omega = acos(cosOmega);
		//the paper says (distA + distB) * sin(.5 * omega) 
		//... and the limit as omega approaches zero becomes the arclength at the average distance:
		//...which makes more sense to me, and seems to fail less often for my data
		double distTransverse = .5 * (distA + distB) * omega; 
		//the paper says fabs(distA + distB)
		//..why would abs matter? it was in the originally cited paper (ref?) 
		//..but most all subsequent papers just prune negative values
		//so I just used fabs(distA - distB) ... where the fabs matters, and the difference is the radial distance
		double distRadial = fabs(distA - distB);	
		return distRadial < radialThreshold && distTransverse < transverseThreshold;
	}
};

//http://arxiv.org/pdf/astro-ph/0310725v2.pdf
struct MergeTest_PowerSpectrumPaper {
	Stat radialDifference;
	Stat transverseDifference;
	double distanceCount;
	double densityCutoff;	//200 used for sdss3 
	double rTransverseMax;	//5 used for sdss3 
	double distanceThreshold;

	MergeTest_PowerSpectrumPaper() 
	: distanceCount(0),
		densityCutoff(1),	//threshold()
		rTransverseMax(50),	//threshold()
		distanceThreshold(.5)	//override to threshold()
	{
	}
	
	bool handleArg(int argc, char **argv, int &k) {
		if (!std::strcmp(argv[k], "--density-cutoff") && k < argc-1) {
			densityCutoff = atof(argv[++k]);
		} else if (!std::strcmp(argv[k], "--transverse-max") && k < argc-1) {
			rTransverseMax = atof(argv[++k]);
		} else if (!std::strcmp(argv[k], "--threshold") && k < argc-1) {
			distanceThreshold = atof(argv[++k]);
		} else {
			return false;
		}
		return true;
	}

	double threshold(int avgDistBin) {
		//avgDensity is a function of radius at the moment ...
		double avgDensity = avgDistBin >= numberof(avgDensityForRadius) ? 0 : avgDensityForRadius[avgDistBin];
		return pow(4./3.*M_PI*avgDensity*densityCutoff + 1./(rTransverseMax*rTransverseMax*rTransverseMax), -1./3.);
	}

	bool operator()(const vec3f &a_, const vec3f &b_) {
		vec3d a(a_);
		vec3d b(b_);
		vec3d c = (a + b) * .5;
		vec3d unitC = vec3d(c).normalize();
		vec3d ca = a - c;
		vec3d cb = b - c;
		vec3d ab = b - a;

		double radialAB = vec3d::dot(ab, unitC); 
		double transverseAB = (ab - radialAB * unitC).len();
		++distanceCount;
		radialDifference.accum(radialAB, distanceCount);
		transverseDifference.accum(transverseAB, distanceCount);

		double avgDist = c.len();
		int avgDistBin = (int)((double)numberof(avgDensityForRadius) * avgDist / maxAvgDensityDist);
		double dist = sqrt(radialAB * radialAB / 100. + transverseAB * transverseAB);

		//not working so great ... densities must be too big
		//return dist <= threshold(avgDistBin);
		return dist <= distanceThreshold;
	}
};


//MergeTest_PowerSpectrumPaper mergeTest;
MergeTest_Angle mergeTest;

void _main(std::vector<std::string> const & args) {
	// TODO standardize the set / file / dir picker
	std::string datasetname = "allsky";
	std::streamsize maxVtxs = 0;

	bool dontWrite = false;
	bool outputRadialDistribution = false;
	HandleArgs(args, {
		{"--set", {"<set> = specify the dataset. default is 'allsky'.", {[&](std::string s){
			datasetname = s;
		}}}},
		{"--radial-distribution", {"= output data for the density distribution bins.", {[&](){
			outputRadialDistribution = true;
		}}}},
		{"--dont-write", {"= don't write points back out, just print # clusters.", {[&](){
			dontWrite = true;
		}}}},
		{"--max", {"<v> = max vertexes to process.", {std::function<void(int)>([&](int n){
			maxVtxs = n;
		})}}},
#if 0
		{"--file", {"<file>	convert only this file. omit path and ext.", {[&](std::string s){
			gotFile = true;
			basefilenames.push_back(s);
		}}}},
		{"--all", {"convert all files in the <set>/points dir.", {[&](){
			gotDir = true;
		}}}},
#endif
#if 0 // only for PowerSpectrumPaper	
		{"--density-cutoff", {"<v> = sets the density cutoff.  sdss3 default is 200.", {std::function<void(double)>([&](double x){
			mergeTest.densityCutoff = x;
		})}}},
		{"--transverse-max", {"<v> = sets the transverse max.  sdss3 default is 5.", {std::function<void(double)>([&](double x){
			mergeTest.rTransverseMax = x;
		})}}},
		{"--threshold", {"<v> = sets the distance threshold.", {std::function<void(double)>([&](double x){
			mergeTest.distanceThreshold = x;
		})}}},
#endif		
	});

#if 1	//single file / buffer all points at once
	//util.cpp me plz
	std::list<std::string> filenames;
	{
		std::list<std::string> dirFilenames = getDirFileNames(std::string("datasets/") + datasetname + "/points");
		for (std::list<std::string>::iterator i = dirFilenames.begin(); i != dirFilenames.end(); ++i) {
			const std::string &filename = *i;
			std::string base, ext;
			getFileNameParts(filename, base, ext);
			if (ext == "f32") {
				filenames.push_back(filename);
			}
		}
	}

	assert(filenames.size() == 1);

	std::streamsize numVtxs;
	std::string filename = std::string() + "datasets/" + datasetname + "/points/" + *filenames.begin();
	vec3f *vtxs = (vec3f*)getFile(filename, &numVtxs);
	numVtxs /= sizeof(vec3f);
	
	//determine clustering thresholds
	maxAvgDensityDist = 0.;	
	for (vec3f *v = vtxs; v < vtxs + numVtxs; v++) {
		double d = v->len();
		if (d > maxAvgDensityDist) maxAvgDensityDist = d;
	}
	//increase by half-step
	maxAvgDensityDist *= (double)(numberof(avgDensityForRadius)+.5) / (double)numberof(avgDensityForRadius);

	for (vec3f *v = vtxs; v < vtxs + numVtxs; v++) {
		double d = v->len();
		int bin = (int)(numberof(avgDensityForRadius) * d / maxAvgDensityDist);
		avgDensityForRadius[bin]++;
	}

	for (int i = 0; i < numberof(avgDensityForRadius); i++) {
		double innerRadius = (double)i / (double)numberof(avgDensityForRadius);
		double outerRadius = innerRadius + 1. / (double)numberof(avgDensityForRadius);
		double innerVolume = 4./3. * M_PI * innerRadius * innerRadius * innerRadius;
		double outerVolume = 4./3. * M_PI * outerRadius * outerRadius * outerRadius;
		double totalVolume = outerVolume - innerVolume;
		avgDensityForRadius[i] /= totalVolume;
	}

	if (outputRadialDistribution) {
		std::ofstream f(std::string() + "datasets/" + datasetname + "/radial-distribution.txt", std::ios::out);
		f << "radius\tdensity" << std::endl;
		for (int i = 0; i < numberof(avgDensityForRadius); i++) {
			double midr = (((double)i + .5) / (double)numberof(avgDensityForRadius) * maxAvgDensityDist);
			f << midr << "\t" << avgDensityForRadius[i] << std::endl;
		}
		return;
	}

	//do the clustering

	Stat redshift;
	std::vector<Cluster*> clusters;
	for (vec3f *v = vtxs; v < vtxs + numVtxs; v++) {
		Cluster *c = new Cluster();
		c->vtxs.push_back(v);
		clusters.push_back(c);
		//while we're here, accum on the length of the vertex 
		redshift.accum(v->len() * HUBBLE_CONSTANT / SPEED_OF_LIGHT, (double)(v - vtxs) + 1);
	}
	redshift.calcStdDev();	
	std::cout << redshift.rw("redshift") << std::endl;

	std::vector<vec2i> links;

	printf("processing     ");
	for (int ica = 0; ica < clusters.size()-1; ica++) {
		if (maxVtxs > 0 && ica >= maxVtxs) break;
		double frac = (double)ica / (double)(clusters.size()-2);
		int percent = (int)(100. * sqrt(frac));
		printf("\b\b\b\b%3d%%", percent);

		Cluster *ca = clusters[ica];
	//for (std::vector<Cluster*>::iterator cai = clusters.begin(); cai != clusters.end(); ++cai) {
	//	Cluster *ca = *cai;
		
		//use a for-loop so it doesn't get invalidated
		for (int iva = 0; iva < ca->vtxs.size(); iva++) {
			vec3f *va = ca->vtxs[iva];
		//for (std::vector<vec3f*>::iterator vai = ca->vtxs.begin(); vai != ca->vtxs.end(); ++vai) {
		//	vec3f *va = *vai;
	
			for (int icb = ica+1; icb < clusters.size(); icb++) {
				if (maxVtxs > 0 && icb >= maxVtxs) break;
				Cluster *cb = clusters[icb];
			//for (std::vector<Cluster*>::iterator cbi = cai+1; cbi != clusters.end();) {
			//	Cluster *cb = *cbi;
			
				int mergeDest = -1;
				for (std::vector<vec3f*>::iterator vbi = cb->vtxs.begin(); vbi != cb->vtxs.end(); ++vbi) {
					vec3f *vb = *vbi;
					
					if (mergeTest(*va, *vb)) {
						mergeDest = vb - vtxs;
						//break; //if you don't want to see the best candidate
					}
				}

				if (mergeDest != -1) {
					links.push_back(vec2i((int)(va - vtxs), mergeDest));
					
					//then merge all points in ca and cb
					ca->vtxs.insert(ca->vtxs.end(), cb->vtxs.begin(), cb->vtxs.end());
					/*
					for (std::vector<vec3f*>::iterator v = cb->vtxs.begin(); v != cb->vtxs.end(); ++v) {
						ca->vtxs.push_back(*v);
					}
					*/
					//inserting could invalidate ca->vtxs (since it is a std::vector)
					
					//and stop testing this chunk
					//let our loop know not to increment cbi
			
					clusters.erase(clusters.begin() + icb);
				} else {
					icb++;
				}
			}
		}
	}
	printf("\n");

	//std::cout << radialDifference.rw("radialDifference") << transverseDifference.rw("transverseDistance") << std::endl;

	std::cout << "made " << clusters.size() << " clusters" << std::endl;

	typedef int clusterIndex_t;	//just don't exceed 2b clusters 
	int *vtxClusters = new int[numVtxs];
	std::memset(vtxClusters, -1, sizeof(int) * numVtxs);
	for (int clusterIndex = 0; clusterIndex < clusters.size(); ++clusterIndex) {
		Cluster *cluster = clusters[clusterIndex];
		for (std::vector<vec3f*>::iterator vi = cluster->vtxs.begin(); vi != cluster->vtxs.end(); ++vi) {
			int vtxIndex = (*vi) - vtxs;
			vtxClusters[vtxIndex] = clusterIndex;
		}
	}
	
	std::map<int, int> clusterSizes;
	for (int i = 0; i < numVtxs; i++) {
		clusterSizes[vtxClusters[i]]++;
	}
	int singleClusters = 0;
	for (std::map<int,int>::iterator i = clusterSizes.begin(); i != clusterSizes.end(); ++i) {
		if (i->second == 1) {
			singleClusters++;
		} else {
			std::cout << " cluster " << i->first << " has " << i->second << std::endl;
		}
	}
	std::cout << singleClusters << " individual clusters" << std::endl;
	
	{
		std::string base, ext;
		getFileNameParts(*filenames.begin(), base, ext);
		std::string clusterFilename = std::string() + "datasets/" + datasetname + "/points/" + base + ".clusters";
		writeFile(clusterFilename, vtxClusters, numVtxs * sizeof(int));
		std::string linkFilename = std::string() + "datasets/" + datasetname + "/points/" + base + ".links";
		writeFile(linkFilename, &links[0], links.size() * sizeof(vec2i));
	}

	clusters.clear();	
	delete[] (unsigned char *)vtxs;
#endif

#if 0	//TODO octree, unless you can fit all 500m points (and clusters) in memory
	OctreeNode *root = OctreeNode::readSet(datasetname);

	struct CompareIterator {
		vec3f v;
		CompareIterator(const vec3f &v_) : v(v_) {}
		void operator()(OctreeNoe *n) {
			vec3f *vtxs = getFile(n->getFileName());
			vec3f *vtxEnd = vtxs + numPoints;
			for (vec3f *w = vtxs; w < vtxEnd; w++) {
				testMerge(*v, *w);
			}
			delete[] vtxs;
		}
	};

	struct NodeIterator {
		void operator()(OctreeNode *n) {
			vec3f *vtxs = getFile(n->getFileName());
			vec3f *vtxEnd = vtxs + numPoints;
			for (vec3f *v = vtxs; v < vtxEnd; v++) {
				n->recurse<CompareIterator>(CompareIterator(*v));
			}
			delete[] vtxs;
		}
	};

	root->recurse<NodeIterator>(NodeIterator());
#endif
}

int main(int argc, char **argv) {
	try {
		_main({argv, argv + argc});
	} catch (std::exception &t) {
		std::cerr << "error: " << t.what() << std::endl;;
		return 1;
	}
	return 0;
}
