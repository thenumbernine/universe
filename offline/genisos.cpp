/*
what will this do?
calculate density isobar ...
rbf or something around each point
then either marching cubes or (!!) surface nets over the whole thing
*/
#include "vec.h"
#include <iostream>
#include "util.h"
#include "stat.h"

using namespace std;
	
const int binRes = 256;

int ifloor(float f) {
	int i = (int)f;
	if (f < 0) i--;
	return i;
}

streamsize vtxCount = 0;
StatSet totalSet;
float totalSize[3];
vec3f *vtxBuf = NULL;

//TODO this looks like a job for the octree
float getValue(float *fpos) {
	vec3f *vtxEnd = vtxBuf + vtxCount;
	for (vec3f *v = vtxBuf; v < vtxEnd; v++) {
		
	}
}

void getCornerValues(const int *ipos, float *cornerValues) {
	int cipos[3];
	float cfpos[3];
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < 3; j++) {
			cipos[j] = ipos[j] + ((i>>j)&1);
			cfpos[j] = (float)cipos[j] / (float)binRes * totalSize[j] + totalSet.vars()[STATSET_X+j].min;
		}
		cornerValues[i] = getValue(cfpos);
	}
}

int main() {
	vtxBuf = (vec3f*)getFile("datasets/2mrs/points/points.f32", &vtxCount);
	vtxCount /= sizeof(vec3f);
	vec3f *vtxEnd = vtxBuf + vtxCount;

	totalSet.read("datasets/2mrs/stats/total.stats");
	for (int i = 0; i < 3; i++) {
		totalSize[i] = totalSet.vars()[STATSET_X+i].max - totalSet.vars()[STATSET_X+i].min;
	}

	int ipos[3];	//integer position in discretized space
	//start at each point (?)
	//march one way til you find the surface
	//then start crawling
	for (vec3f *v = vtxBuf; v < vtxEnd; v++) {
		float fpos[3];
		for (int i = 0; i < 3; i++) {
			fpos[i] = ((*v)[i] - totalSet.vars()[STATSET_X+i].min) / totalSize[i];
			//if (fx < 0 || fx >= 1 || fy < 0 || fy >= 1 || fz < 0 || fz >= 1) continue;
			fpos[i] *= (float)binRes;
			ipos[i] = ifloor(fpos[i]);
		}
		float cornerValues[8];
		getCornerValues(ipos, cornerValues);

	}
}
