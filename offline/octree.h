#ifndef OCTREE_H
#define OCTREE_H

#include <string>

#include "vec.h"
#include "box.h"

struct OctreeNode {
	box3f bbox, usedBBox;	
	bool leaf;
	int whichChild;	//-1 = root, 0-7 = the parent's child's index
	int numPoints;
	OctreeNode *parent;
	OctreeNode *ch[8];
	const static int splitThreshold = 200000;

	//root ctor
	OctreeNode(const vec3f &min_, const vec3f &max_);
	//child ctor
	OctreeNode(OctreeNode *parent_, int whichChild_);
	//arbitrary ctor
	OctreeNode(OctreeNode *parent_, int whichChild_, const vec3f &min_, const vec3f &max_);
	
	virtual ~OctreeNode();
	std::string getFileNameBase();
	virtual std::string getFileName();	
	static bool contains(const box3f &b, const vec3f &v);

	static OctreeNode *readSet(const std::string &setname);
};

#endif

