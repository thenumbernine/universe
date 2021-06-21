#include "octree.h"
#include <fstream>

//root ctor
OctreeNode::OctreeNode(const vec3f &min_, const vec3f &max_) 
:	leaf(true),
	parent(NULL),
	whichChild(-1),
	numPoints(0),
	bbox(box3f(min_, max_)),
	usedBBox(box3f(vec3f(INFINITY), vec3f(-INFINITY)))
{
	memset(ch, 0, sizeof(ch));
}

//child ctor 
OctreeNode::OctreeNode(OctreeNode *parent_, int whichChild_) 
: 	leaf(true),
	parent(parent_),
	whichChild(whichChild_),
	numPoints(0),
	usedBBox(box3f(vec3f(INFINITY), vec3f(-INFINITY)))
{
	vec3f parentCenter = parent->bbox.center();
	for (int i = 0; i < 3; i++) {
		bool axisPlus = !!(whichChild & (1 << i));	
		bbox.min[i] = axisPlus ? parentCenter[i] : parent->bbox.min[i];
		bbox.max[i] = axisPlus ? parent->bbox.max[i] : parentCenter[i];
	}
	memset(ch, 0, sizeof(ch));
}


//child ctor with arbitrary bbox
OctreeNode::OctreeNode(OctreeNode *parent_, int whichChild_, const vec3f &min_, const vec3f &max_) 
: 	leaf(true),
	parent(parent_),
	whichChild(whichChild_),
	numPoints(0),
	bbox(box3f(min_, max_)),
	usedBBox(box3f(vec3f(INFINITY), vec3f(-INFINITY)))
{
	memset(ch, 0, sizeof(ch));
}

OctreeNode::~OctreeNode() {
	for (int i = 0; i < numberof(ch); i++) {
		if (ch[i]) delete ch[i];
	}
}

std::string OctreeNode::getFileNameBase() {
	if (!parent) return "octree/node";
	std::string parentName = parent->getFileNameBase();
	assert(whichChild >= 0 && whichChild < numberof(ch));
	parentName += ('a' + whichChild);
	return parentName;
}

std::string OctreeNode::getFileName() {
	return getFileNameBase() + ".f32";
}

bool OctreeNode::contains(const box3f &b, const vec3f &v) {
	return b.max.x >= v.x
		&& b.max.y >= v.y
		&& b.max.z >= v.z
		&& b.min.x <= v.x
		&& b.min.y <= v.y
		&& b.min.z <= v.z;
}

#include <list>
#include <algorithm>
#include "util.h"	//getFileNameParts
#include "stat.h"
#include "exception.h"

OctreeNode *OctreeNode::readSet(const std::string &datasetname) {
	std::list<std::string> files = getDirFileNames(std::string("datasets/") + datasetname + "/octree");
	for (auto i = files.begin(); i != files.end(); ) {
		std::string base, ext;
		getFileNameParts(*i, base, ext);
		if (ext != "f32") {
			i = files.erase(i);
		} else {
			i++;
		}
	}


	//sort by name
	files.sort();
	//int numNodesWithPoints = 0;	
	OctreeNode *root = NULL;
	//last i checked stl sort grouped by length then sorted alphabetically  -- just what i'm looking for
	for (auto const & filename : files) {
		//pick out suffix: "node<suffix>.f32"
		//use it to determine node order
		std::string ident = filename.substr(4, filename.length()-8);
		//cout << "loading ident " << ident << endl;
		int identLength = ident.length();
		if (!identLength) {
			if (root) throw Exception() << "found two roots";
			
			StatSet totalStats;
			totalStats.read((std::string("datasets/") + datasetname + "/stats/total.stats").c_str());
			box3f bbox;
			for (int j = 0; j < 3; j++) {
				bbox.min[j] = totalStats.vars()[STATSET_X+j].min;
				bbox.max[j] = totalStats.vars()[STATSET_X+j].max;
			}
			root = new OctreeNode(NULL, -1, bbox.min, bbox.max);
		
			//numNodesWithPoints++;
		} else {
			OctreeNode *node = root;
			for (int j = 0; j < identLength; j++) {
				int childIndex = ident[j] - 'a';
				assert(childIndex >= 0 && childIndex < 8);
				if (!node->ch[childIndex]) {
					node->leaf = false;
					node->ch[childIndex] = new OctreeNode(node, childIndex);
				}
				node = node->ch[childIndex];
			}
			//now read the points into 'node'
			node->numPoints = getFileSize(node->getFileName()) / sizeof(vec3f);

			//numNodesWithPoints++;
		}
	}
	//cout << "numNodesWithPoints " << numNodesWithPoints << endl;	
	return root;
}

