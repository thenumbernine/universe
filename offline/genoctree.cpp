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
#include <iostream>
#include <fstream>
#include <filesystem>

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
	virtual std::string getFileName();

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
	using ArgType = std::string;
	std::string desc(const ArgType &basename);
	void operator()();
	void operator()(const ArgType &basename);
};

#ifdef USE_CACHE_V1

#include "mrucache.h"

struct WritableOctreeNodeWriteCacheEntry {
	using ArgType = WritableOctreeNode;
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

MRUCache<std::string, FILE *> cache(openCacheFile, closeCacheFile, 50);

#endif	//USE_CACHE_V2

#ifdef USE_SINGLE_WRITE_BUFFER
vector<vec3f> writeBuffer;
WritableOctreeNode *lastWriteNode = nullptr;

void finalizeWriteBuffer() {
	if (lastWriteNode) { 
		FILE *fp = fopen(lastWriteNode->getFileName().c_str(), "ab");
		assert(fp);
		fwrite(&writeBuffer[0], sizeof(vec3f), writeBuffer.size(), fp);
		fclose(fp);

		std::cout << lastWriteNode << " " << writeBuffer.size() << std::endl;;
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

std::string datasetname = "allsky";
WritableOctreeNode *root= nullptr;
std::list<std::string> basefilenames;

WritableOctreeNode::WritableOctreeNode(const vec3f &min_, const vec3f &max_)
: OctreeNode(min_, max_) {}

WritableOctreeNode::WritableOctreeNode(WritableOctreeNode *parent_, int whichChild_)
: OctreeNode(parent_, whichChild_) {}

WritableOctreeNode::WritableOctreeNode(WritableOctreeNode *parent_, int whichChild_, const vec3f &min_, const vec3f &max_)
: OctreeNode(parent_, whichChild_, min_, max_) {}

std::string WritableOctreeNode::getFileName() {
	return std::string("datasets/") + datasetname + "/" + OctreeNode::getFileName();
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
				std::cout << "splitting " << getFileName() 
					<< " numpoints " << numPoints 
					<< " bbox " << bbox
					<< " usedBBox " << usedBBox
					<< std::endl;;
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
			std::string filename = getFileName();
			vec3f *vtxbuf = (vec3f*)getFile(getFileName().c_str(), nullptr);
		
			//...so we can delete the file ...
			remove(filename.c_str());
	
			//... before iterating through its points and adding them to the child node (which may do the same thing if all points fall into one child)
			for (vec3f *v = vtxbuf; v < vtxbuf + numPoints; v++) {
				addToChild(*v, true);	//don't split, that could be recursive, then we would be allocatnig too much for one addPoint oepration
			}
			delete[] vtxbuf;
			numPoints = 0;
			
			if (VERBOSE) {
				std::cout << getFileName() << " post-split child stats: " << std::endl;;
				for (int i = 0; i < numberof(ch); i++) {
					if (ch[i]) std::cout << "\tchild " << i << " name " << ch[i]->getFileName() << " num pts " << ch[i]->numPoints << std::endl;; 
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
		if (VERBOSE) std::cout << "allocating child " << childIndex << std::endl;;
		
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
	//if (VERBOSE) std::cout << "adding to child " << childIndex << std::endl;;
	static_cast<WritableOctreeNode*>(ch[childIndex])->addPoint(v, dontSplit);
}

OctreeWorker::OctreeWorker() : usedCount(0), unusedCount(0) {}

void OctreeWorker::init() {
	std::string totalStatFilename = std::string() + "datasets/" + datasetname + "/stats/total.stats";
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

std::string OctreeWorker::desc(const ArgType &basename) { 
	return std::string() + "file " + basename; 
}

void OctreeWorker::operator()() {
	for (auto const & i : basefilenames) {
		(*this)(i);
	}
}

void OctreeWorker::operator()(const ArgType &basename) {
	std::string ptfilename = std::string() + "datasets/" + datasetname + "/points/" + basename + ".f32";

	std::streamsize vtxbufsize = 0;
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
				std::cout << "used points: " << usedCount << std::endl;;
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

void _main(std::vector<std::string> const & args) {
	bool gotDir = false;
	bool gotFile = false;

	auto h = HandleArgs(args, {
		{"--set", {"<set> = specify the dataset. default is 'allsky'.", {[&](std::string s){
			datasetname = s;
		}}}},
		{"--file", {"<file>	convert only this file. omit path and ext.", {[&](std::string s){
			gotFile = true;
			basefilenames.push_back(s);
		}}}},
		{"--all", {"convert all files in the <set>/points dir.", {[&](){
			gotDir = true;
		}}}},
		{"--verbose", {"shows verbose information.", {[&](){
			VERBOSE = 1;
		}}}},
		{"--wait", {"waits for key at each entry.  implies verbose.", {[&](){
			INTERACTIVE = 1;
		}}}},
	});
	
	if (!gotDir && !gotFile) {
		h.showhelp();
		return;
	}

	std::filesystem::create_directory(std::string() + "datasets/" + datasetname + "/octree");

	if (gotDir) {
		std::list<std::string> dirFilenames = getDirFileNames(std::string() + "datasets/" + datasetname + "/points");
		for (auto const & i : dirFilenames) {
			std::string base, ext;
			getFileNameParts(i, base, ext);
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
	std::cout << (deltaTime / (double)basefilenames.size()) << " seconds per file" << std::endl;;

	//done
	worker.done();

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
