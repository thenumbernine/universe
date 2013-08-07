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
  so for that it would be best to RLE the cluster hash list files


- RLE the cluster hash list
- store cluster hashes in a parallel fileset to the octree
- assume no file means RLE'd 0's all through
- and assume cluster hash 0 means "unique hash all to myself"

... now the next challenge is, how do we "iterate through a cluster" ?



*/
#include <string.h>

#include <string>
#include <iostream>
#include <vector>
#include <list>
#include <map>
#include <fstream>

#include "octree.h"
#include "defs.h"
#include "util.h"
#include "exception.h"
#include "stat.h"

using namespace std;

struct Cluster {
	vector<vec3f*> vtxs;
};

//used by method #1, from Angel's paper
double radialThreshold = 1.;	//bRadial * pow(meanDensityOfGalaxies, -1./3.);
double tangentialThreshold = radialThreshold / 8.;

//used by the paper Angel cites
double avgDensityForRadius[5];
double maxAvgDensityDist = 0;

Stat angleDifference;
Stat redshiftDifference;

bool testMerge(const vec3d &a, const vec3d &b) {
#if 0
	//radial distance in Mpc
	double distA = a.len();	
	double distB = b.len();
	//redshift (z) = distance Mpc * Hubble constant km/s/Mpc / speed of light km/s
	double velA = distA * HUBBLE_CONSTANT / SPEED_OF_LIGHT;
	double velB = distB * HUBBLE_CONSTANT / SPEED_OF_LIGHT;
	//redshift (z)
	vec3d unitA = a * (1. / distA);
	vec3d unitB = b * (1. / distB);
	double cosOmega = dot(unitA, unitB);
	if (cosOmega < 1.) cosOmega = 1.;
	if (cosOmega > 1.) cosOmega = 1.;
	double omega = acos(cosOmega);
	double distRadial = (distA + distB) * sin(omega);
	double distTangential = fabs(distA + distB);	//why would abs matter? it was in the originally cited paper (ref?) but most all subsequent papers just prune negative values
	return (distRadial < radialThreshold
	&& distTangential < tangentialThreshold);
#endif
#if 1
	//http://arxiv.org/pdf/astro-ph/0310725v2.pdf
	vec3d c = (a + b) * .5;
	vec3d unitC = vec3d(c).normalize();
	vec3d ca = a - c;
	vec3d cb = b - c;
	vec3d ab = b - a;

	double radialAB = vec3d::dot(ab, unitC); 
	double transverseAB = (ab - radialAB * unitC).len();

	double avgDist = c.len();
	int avgDistBin = (int)((double)numberof(avgDensityForRadius) * avgDist / maxAvgDensityDist);
	double avgDensity = avgDistBin >= numberof(avgDensityForRadius) ? 0 : avgDensityForRadius[avgDistBin];
	//avgDensity is a function of space ...
	const double overdensity = 200.;
	const double rTransverseMax = 5.;	//used for sdss at least
	return sqrt((radialAB * radialAB / 100. + transverseAB * transverseAB)
		<= pow(4./3.*M_PI*avgDensity*(1.+overdensity) + 1./(rTransverseMax*rTransverseMax*rTransverseMax), -1./3.));
#endif
}

void showhelp() {
	cout
	<< "usage: cluster <options>" << endl
	<< "options:" << endl
	<< "	--set <set>			specify the dataset. default is 'allsky'." << endl
	//<< "	--file <file>		convert only this file.  omit path and ext." << endl
	//<< "	--all				convert all files in the <set>/points dir." << endl
	<< "	--radial-distribution	output data for the density distribution bins." << endl
	<< "	--dont-write		don't write points back out, just print # clusters" << endl
	;
}

int main(int argc, char **argv) {
	// TODO standardize the set / file / dir picker
	string datasetname = "allsky";

	bool dontWrite = false;
	bool outputRadialDistribution = false;
	for (int k = 1; k < argc; k++) {
		if (!strcmp(argv[k], "--set") && k < argc-1) {
			datasetname = argv[++k];
		} else if (!strcmp(argv[k], "--radial-distribution")) {
			outputRadialDistribution = true;
		} else if (!strcmp(argv[k], "--dont-write")) {
			dontWrite = true;
		} else {
			showhelp();
			return 0;
		}
	}

#if 1	//single file / buffer all points at once
	//util.cpp me plz
	list<string> filenames;
	{
		list<string> dirFilenames = getDirFileNames(string("datasets/") + datasetname + "/points");
		for (list<string>::iterator i = dirFilenames.begin(); i != dirFilenames.end(); ++i) {
			const string &filename = *i;
			string base, ext;
			getFileNameParts(filename, base, ext);
			if (ext == "f32") {
				filenames.push_back(filename);
			}
		}
	}

	assert(filenames.size() == 1);

	streamsize numVtxs;
	string filename = string() + "datasets/" + datasetname + "/points/" + *filenames.begin();
	vec3f *vtxs = (vec3f*)getFile(filename, &numVtxs);
	numVtxs /= sizeof(vec3f);

	int *vtxClusters = NULL;
	{
		string base, ext;
		getFileNameParts(*filenames.begin(), base, ext);
		streamsize vtxClusterFileSize;
		vtxClusters = (int*)getFile(string("datasets/") + datasetname + "/points/" + base + ".clusters", &vtxClusterFileSize);
		assert(vtxClusterFileSize == numVtxs * sizeof(int));
	}

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
		ofstream f((string("datasets/") + datasetname + "/radial-distribution.txt").c_str(), ios::out);
		for (int i = 0; i < numberof(avgDensityForRadius); i++) {
			double midr = (((double)i + .5) / (double)numberof(avgDensityForRadius) * maxAvgDensityDist);
			f << midr << "\t" << avgDensityForRadius[i] << endl;
		}
		return 0;
	}

	map<int, int> clusterSizes;
	for (int i = 0; i < numVtxs; i++) {
		int clusterIndex = vtxClusters[i];	//find the cluster index of the i'th point
		clusterSizes[clusterIndex]++;
	}
	int singleClusters = 0;
	for (map<int,int>::iterator i = clusterSizes.begin(); i != clusterSizes.end(); ++i) {
		if (i->second == 1) {
			singleClusters++;
		} else {
			cout << " cluster " << i->first << " has " << i->second << endl;
		}
	}
	cout << singleClusters << " individual clusters" << endl;

	vector<Cluster*> clusters;
	for (int i = 0; i < numVtxs; i++) {
		int clusterIndex = vtxClusters[i];	//find the cluster index of the i'th point
		clusterSizes[clusterIndex]++;
		if (clusters.size() <= clusterIndex) {
			int oldsize = clusters.size();
			clusters.resize(clusterIndex+1);	//make sure we have room for it in our list of clusters 
			for (int j = oldsize; j < clusters.size(); j++) {
				clusters[j] = new Cluster();	//maybe i should use vector<Clsuter> clusters ?
			}
		}
		Cluster *ci = clusters[clusterIndex];
		assert(ci);
		ci->vtxs.push_back(vtxs+i);	//add this vertex to that cluster index
	}
	
	//now that we made them, find the radial and transverse "dispersions"
	// ... this could be stddev or it could be min/max ...
	//then, if the radial exceeds the transverse, deem it a "Finger of God" and recompress it radially to make it round
	int squashCount = 0;
	int clusterIndex = 0;
	for (vector<Cluster*>::iterator ic = clusters.begin(); ic != clusters.end(); ++ic, ++clusterIndex) {
		Cluster *c = *ic; 
		if (c->vtxs.size() == 1) continue;
		cout << "cluster " << clusterIndex << " size " << c->vtxs.size() << endl;
		vec3d center;
		for (vector<vec3f*>::iterator iv = c->vtxs.begin(); iv != c->vtxs.end(); ++iv) {
			const vec3f &v = **iv;
			//cout << "\tpt" << v << endl;
			center += (vec3d)v;
		}
		center *= 1. / (double)c->vtxs.size();
		vec3d unitC = vec3d(center).normalize();
		Stat absRadialStat, transverseStat;
		double n = 0;
		for (vector<vec3f*>::iterator iv = c->vtxs.begin(); iv != c->vtxs.end(); ++iv) {
			const vec3f &v = **iv; 
			vec3d d = (vec3d)v - center;
			double radial = vec3d::dot(d, unitC);
			double transverse = (d - radial * unitC).len();
			double absRadial = fabs(radial);
			n++;
			absRadialStat.accum(absRadial, n);
			transverseStat.accum(transverse, n);
		}
		absRadialStat.calcStdDev();
		transverseStat.calcStdDev();
		//cout << absRadialStat.rw("absRadial") << transverseStat.rw("transverse") << endl;
		if (absRadialStat.max > transverseStat.max) {
			squashCount++;
			double radialSquashScalar = transverseStat.max / absRadialStat.max;
			for (vector<vec3f*>::iterator iv = c->vtxs.begin(); iv != c->vtxs.end(); ++iv) {
				vec3f &v = **iv; 
				vec3d d = (vec3d)v - center;
				double radial = vec3d::dot(d, unitC);
				vec3d transverse = d - radial * unitC;
				//re-stretch radial component
				radial *= radialSquashScalar;
				//re-apply it to the difference-to-center
				d = transverse + unitC * radial;
				//re-apply it to the start vector
				v = center + d;
			}
		}
	}
	cout << "squashed " << squashCount << " clusters" << endl;
	
	//now we write back out!
	if (!dontWrite) {
		assert(filenames.size() == 1);
		string filename = string() + "datasets/" + datasetname + "/points/" + *filenames.begin();
		writeFile(filename, vtxs, sizeof(vec3f) * numVtxs);
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

	return 0;
}
