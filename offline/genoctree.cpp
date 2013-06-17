/*
how many leafs do we want?
lets say 5k leaf nodes
we have 500mil points, divide by 5k and we get 100k points per bin


Google Earth:
root level: 4x4 tiles @ 256x256 pixels
16 zoom levels
total number of nodes: sum i=2 to 17 of 4^i = 4^2 * sum i=0,15 of 4^i = 16 * (4^16 - 1) / 3 = 2^4 * (2^32 - 1) / 3 = (64G - 1) / 3 
= 22,906,492,240 nodes if full (and it's not full)

how should we name these files?
node.f32 for the root, node[a-h] for depth 1, node[a-h][a-h] for depth 2, etc

*/
#include <direct.h>

#include <iostream>
#include <string>
#include <vector>
#include <fstream>

#include "exception.h"
#include "stat.h"
#include "util.h"
#include "batch.h"
#include "octree.h"


//#define DONT_USE_CACHE			//separate fopen and fclose for each point
//#define USE_SINGLE_WRITE_BUFFER		//use a single write buffer, write out points when the target node changes
#define USE_CACHE_V1				//MRU fp cache
//#define USE_CACHE_V2
//#define USE_WRITE_BUFFER_PER_NODE	//write buffer per node, write them out once total size exceeds some threshold

using namespace std;

int INTERACTIVE = 0;
int VERBOSE = 0;

struct WritableOctreeNodeWriteCacheEntry;

struct WritableOctreeNode : public OctreeNode {
	//root ctor
	WritableOctreeNode(const vec3f &min_, const vec3f &max_);
	//child ctor
	WritableOctreeNode(WritableOctreeNode *parent_, int whichChild_);
	//arbitrary ctor
	WritableOctreeNode(WritableOctreeNode *parent_, int whichChild_, const vec3f &min_, const vec3f &max_);
	
protected:
	WritableOctreeNode* getChild(int idx) {return static_cast<WritableOctreeNode*>(OctreeNode::ch[idx]);}
	void setChild(int idx, WritableOctreeNode *t) {OctreeNode::ch[idx] = t;}
public:

	void writePoint(const vec3f &v);
	void addToChild(const vec3f &v, bool dontSplit = false);
	void addPoint(const vec3f &v, bool dontSplit = false);
	virtual string getFileName();

#ifdef USE_WRITE_BUFFER_PER_NODE
	vector<vec3f> writeBuffer;
	void flush();
	void flushAll();
#endif	//USE_WRITE_BUFFER_PER_NODE
};

struct OctreeWorker {
	StatSet totalStats;
	int usedCount, unusedCount;

	OctreeWorker();

	void init();
	void done();
	
	//for the batch processor
	typedef string ArgType;
	string desc(const ArgType &basename);
	void operator()();
	void operator()(const ArgType &basename);
};

#ifdef USE_CACHE_V1

#include "mrucache.h"

struct WritableOctreeNodeWriteCacheEntry {
	typedef WritableOctreeNode ArgType;
	ArgType *obj;
	FILE *fp;
	WritableOctreeNodeWriteCacheEntry(ArgType *obj_) : obj(obj_) {
		fp = fopen(obj->getFileName().c_str(), "ab"); 
	};
	~WritableOctreeNodeWriteCacheEntry() {
		fclose(fp);
	}
	const ArgType *getArg() const { return obj; }
};

/*
should I use two args, since the second's ctor must be the first?
	-- or should i have the second inherit from a default, or default to, a standard encapsulation of the first?
		although a standard encapsulation will never be used.  custom behavior is almost always guaranteed.
	-- or should i have the first inferred from a typedef of the second?
	-- I'll have the get/set arg inferred from a typedef of the member arg
*/
MRUCache<WritableOctreeNodeWriteCacheEntry> cache(50);
#endif	//USE_CACHE_V1

#ifdef USE_CACHE_V2

#include "mrucache2.h"

FILE *openCacheFile(std::string filename) {
	return fopen(filename.c_str(), "ab");
}

void closeCacheFile(FILE *fp) {
	fclose(fp);
}

MRUCache<string, FILE *> cache(openCacheFile, closeCacheFile, 50);

#endif	//USE_CACHE_V2

#ifdef USE_SINGLE_WRITE_BUFFER
vector<vec3f> writeBuffer;
WritableOctreeNode *lastWriteNode = NULL;

void finalizeWriteBuffer() {
	if (lastWriteNode) { 
		FILE *fp = fopen(lastWriteNode->getFileName().c_str(), "ab");
		assert(fp);
		fwrite(&writeBuffer[0], sizeof(vec3f), writeBuffer.size(), fp);
		fclose(fp);

		cout << lastWriteNode << " " << writeBuffer.size() << endl;
		writeBuffer.resize(0);
	} else {
		assert(!writeBuffer.size());
	}
}

void writeBufferAddPoint(WritableOctreeNode *node, const vec3f &v) {
	if (node != lastWriteNode) {
		finalizeWriteBuffer();
		lastWriteNode = node;
	}
	writeBuffer.push_back(v);
}
#endif	//USE_SINGLE_WRITE_BUFFER

#ifdef USE_WRITE_BUFFER_PER_NODE

int totalPointsBuffered = 0;
const int totalPointThreshold = 1000000;

void WritableOctreeNode::flush() {
	if (!writeBuffer.size()) return;
	
	FILE *fp = fopen(getFileName().c_str(), "ab");
	assert(fp);
	fwrite(&writeBuffer[0], sizeof(vec3f), writeBuffer.size(), fp);
	fclose(fp);
	
	totalPointsBuffered -= writeBuffer.size();
	writeBuffer.resize(0);
}

void WritableOctreeNode::flushAll() {
	flush();
	
	for (int i = 0; i < numberof(ch); i++) {
		if (ch[i]) getChild(i)->flushAll();
	}
}

#endif	//USE_WRITE_BUFFER_PER_NODE

string datasetname = "allsky";
WritableOctreeNode *root= NULL;
list<string> basefilenames;

WritableOctreeNode::WritableOctreeNode(const vec3f &min_, const vec3f &max_)
: OctreeNode(min_, max_) {}

WritableOctreeNode::WritableOctreeNode(WritableOctreeNode *parent_, int whichChild_)
: OctreeNode(parent_, whichChild_) {}

WritableOctreeNode::WritableOctreeNode(WritableOctreeNode *parent_, int whichChild_, const vec3f &min_, const vec3f &max_)
: OctreeNode(parent_, whichChild_, min_, max_) {}

string WritableOctreeNode::getFileName() {
	return string("datasets/") + datasetname + "/" + OctreeNode::getFileName();
}

void WritableOctreeNode::addPoint(const vec3f &v, bool dontSplit) {
	assert(contains(bbox, v));

	//traverse down the tree until we reach a leaf. 
	//add the node. 
	//see if the child needs to be split.
	if (leaf) {
		writePoint(v);

		/*
		lots of splits are happening where all the children are in one subsequent child node, so the next child needs to be split just as quickly
		in the end, you end up with a tree of N levels deep for the points grouped within 2^-N fraaction of the total size
		fixes? 
		- BSP, or axis-aligned tree (rather than octree with split axis on center) this might balance up-front data, but might remain skewed for once the whole set is added
		- don't add unless all children areas have > M nodes (for some M) ... but this might result in points clumping up in an area if they just happen to all lie on whichever side of the plane of this node
		- max tree depth.  this wouldn't help the denser areas ... 
		*/
		if (numPoints >= splitThreshold && !dontSplit) {
			if (VERBOSE) {
				cout << "splitting " << getFileName() 
					<< " numpoints " << numPoints 
					<< " bbox " << bbox
					<< " usedBBox " << usedBBox
					<< endl;
			}
			leaf = false;
	
#ifdef USE_CACHE_V1
			//remove file pointer from write queue ... we won't be writing anymore
			cache.removeEntry(this);
#endif	//USE_CACHE_V1
#ifdef USE_CACHE_V2
			cache.remove(getFileName());
#endif	//USE_CACHE_V2
#ifdef USE_SINGLE_WRITE_BUFFER
			finalizeWriteBuffer();
#endif	//USE_SINGLE_WRITE_BUFFER
#ifdef USE_WRITE_BUFFER_PER_NODE
			flush();
#endif	//USE_WRITE_BUFFER_PER_NODE

			//load file contents into ram...
			string filename = getFileName();
			vec3f *vtxbuf = (vec3f*)getFile(getFileName().c_str(), NULL);
		
			//...so we can delete the file ...
			remove(filename.c_str());
	
			//... before iterating through its points and adding them to the child node (which may do the same thing if all points fall into one child)
			for (vec3f *v = vtxbuf; v < vtxbuf + numPoints; v++) {
				addToChild(*v, true);	//don't split, that could be recursive, then we would be allocatnig too much for one addPoint oepration
			}
			delete[] vtxbuf;
			numPoints = 0;
			
			if (VERBOSE) {
				cout << getFileName() << " post-split child stats: " << endl;
				for (int i = 0; i < numberof(ch); i++) {
					if (ch[i]) cout << "\tchild " << i << " name " << ch[i]->getFileName() << " num pts " << ch[i]->numPoints << endl; 
				}
			}
		}
	} else {
		addToChild(v);
	}
}

void WritableOctreeNode::writePoint(const vec3f &v) {
#ifdef DONT_USE_CACHE
	FILE *fp = fopen(getFileName().c_str(), "ab");
	assert(fp);
	fwrite(&v, sizeof(v), 1, fp);
	fclose(fp);
#endif	//DONT_USE_CACHE
#ifdef USE_CACHE_V1
	WritableOctreeNodeWriteCacheEntry *fe = cache.getEntry(this);
	fwrite(&v, sizeof(v), 1, fe->fp);
#endif	//USE_CACHE_V1
#ifdef USE_CACHE_V2
	FILE *fp = cache.get(getFileName());
	fwrite(&v, sizeof(v), 1, fp);
	long int filesize = ftell(fp);
	assert(filesize != -1L);
#endif	//USE_CACHE_V2
#ifdef USE_SINGLE_WRITE_BUFFER
	writeBufferAddPoint(this, v);
#endif	//USE_SINGLE_WRITE_BUFFER
#ifdef USE_WRITE_BUFFER_PER_NODE
	writeBuffer.push_back(v);
	totalPointsBuffered++;
	if (totalPointsBuffered > totalPointThreshold) {
		//then flush them all ...
		root->flushAll();
	}
#endif	//USE_WRITE_BUFFER_PER_NODE

	for (int i = 0; i < 3; i++) {
		if (v[i] < usedBBox.min[i]) usedBBox.min[i] = v[i];
		if (v[i] > usedBBox.max[i]) usedBBox.max[i] = v[i];
	}

	numPoints++;
}

void WritableOctreeNode::addToChild(const vec3f &v, bool dontSplit) {
	assert(contains(bbox, v));

	vec3f center = bbox.center();
	
	//pick the child index based on which side of the center axii the point lies
	//+axis nodes are >, so -axis nodes should be <= 
	int childIndex = (v.x > center.x) |
			((v.y > center.y) << 1) |
			((v.z > center.z) << 2);
	
	//if the child index doesn't exist then create it
	if (!ch[childIndex]) {
		if (VERBOSE) cout << "allocating child " << childIndex << endl;
		
		//determine the child bbox by the child index
		WritableOctreeNode *chn = new WritableOctreeNode(this, childIndex);
		setChild(childIndex, chn);
		for (int i = 0; i < 3; i++) {
			assert(chn->bbox.min[i] <= v[i]);
			assert(chn->bbox.max[i] > v[i]);
		}
	}
	if (!contains(ch[childIndex]->bbox, v)) {
		throw Exception() << "created a child of node " << getFileName() << " for point " << v << " that couldn't hold the point";
	}
	//if (VERBOSE) cout << "adding to child " << childIndex << endl;
	static_cast<WritableOctreeNode*>(ch[childIndex])->addPoint(v, dontSplit);
}

OctreeWorker::OctreeWorker() : usedCount(0), unusedCount(0) {}

void OctreeWorker::init() {
	string totalStatFilename = string("datasets/") + datasetname + "/stats/total.stats";
	totalStats.read(totalStatFilename.c_str());
	totalStats.calcSqAvg();

	vec3f rootMin, rootMax;
	for (int i = 0; i < 3; i++) {
		rootMin[i] = totalStats.vars()[STATSET_X+i].min;
		rootMax[i] = totalStats.vars()[STATSET_X+i].max;
	}
	
	root = new WritableOctreeNode(rootMin, rootMax);
}

void OctreeWorker::done() {
#ifdef USE_SINGLE_WRITE_BUFFER
	finalizeWriteBuffer();
#endif	//USE_SINGLE_WRITE_BUFFER
#ifdef USE_WRITE_BUFFER_PER_NODE 
	root->flushAll();
#endif	//USE_WRITE_BUFFER_PER_NODE 
}

string OctreeWorker::desc(const ArgType &basename) { 
	return string() + "file " + basename; 
}

void OctreeWorker::operator()() {
	for (list<string>::iterator i = basefilenames.begin(); i != basefilenames.end(); ++i) {
		this->operator()(*i);
	}
}

void OctreeWorker::operator()(const ArgType &basename) {
	string ptfilename = string("datasets/") + datasetname + "/points/" + basename + ".f32";

	streamsize vtxbufsize = 0;
	vec3f *vtxbuf = (vec3f*)getFile(ptfilename, &vtxbufsize);
	vec3f *vtxbufend = vtxbuf + (vtxbufsize / sizeof(vec3f));
	for (vec3f *vtx = vtxbuf; vtx < vtxbufend; vtx++) { 
		if ((*vtx)[0] < totalStats.vars()[0].min ||
			(*vtx)[0] > totalStats.vars()[0].max ||
			(*vtx)[1] < totalStats.vars()[1].min ||
			(*vtx)[1] > totalStats.vars()[1].max ||
			(*vtx)[2] < totalStats.vars()[2].min ||
			(*vtx)[2] > totalStats.vars()[2].max)
		{
			unusedCount++;
			continue;
		}
		
		usedCount++;
		if (VERBOSE) {
			if (!(usedCount % WritableOctreeNode::splitThreshold)) {
				cout << "used points: " << usedCount << endl;
			}
		}
		root->addPoint(*vtx);
		
		if (INTERACTIVE) {
			if (getchar() == 'q') {
				exit(0);	
			}
		}
	}
}

void showhelp() {
	cout
	<< "usage: genoctree <options>" << endl
	<< "options:" << endl
	<< "	--set <set>		specify the dataset. default is 'allsky'." << endl
	<< "	--file <file>	convert only this file.  omit path and ext." << endl
	<< "	--all			convert all files in the <set>/points dir." << endl
	<< "	--verbose		shows verbose information." << endl
	<< "	--wait			waits for key at each entry.  implies verbose." << endl
	;
}

int main(int argc, char **argv) {
	try {
		bool gotDir = false, gotFile = false;

		for (int k = 1; k < argc; k++) {
			if (!strcmp(argv[k], "--verbose")) {
				VERBOSE = 1;
			} else if (!strcmp(argv[k], "--wait")) {
				INTERACTIVE = 1;
			} else if (!strcmp(argv[k], "--set") && k < argc-1) {
				datasetname = argv[++k];
			} else if (!strcmp(argv[k], "--all")) {
				gotDir = true;
			} else if (!strcmp(argv[k], "--file") && k < argc-1) {
				gotFile = true;
				basefilenames.push_back(argv[++k]);
			} else {
				showhelp();
				return 0;
			}
		}
		
		if (!gotDir && !gotFile) {
			showhelp();
			return 0;
		}

		mkdir((string("datasets/") + datasetname + "/octree").c_str());

		if (gotDir) {
			list<string> dirFilenames = getDirFileNames(string("datasets/") + datasetname + "/points");
			for (list<string>::iterator i = dirFilenames.begin(); i != dirFilenames.end(); ++i) {
				string base, ext;
				getFileNameParts(*i, base, ext);
				if (ext == "f32") {
					basefilenames.push_back(base);
				}
			}
		}

		OctreeWorker worker;

		//init
		worker.init();

		//profile

		double deltaTime = profile("getstats", worker);
		cout << (deltaTime / (double)basefilenames.size()) << " seconds per file" << endl;

		//done
		worker.done();

	} catch (exception &t) {
		cerr << "error: " << t.what() << endl;
		return 1;
	}
	return 0;
}

