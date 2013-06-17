#include <sys/time.h>
#include <dirent.h>

#include <fstream>

#include "util.h"
#include "exception.h"

using namespace std;

bool fileexists(const string &filename) {
	ifstream f(filename.c_str());
	return f.is_open();
}

double getTime() {
	timeval t;
	gettimeofday(&t, NULL);
	return (double)t.tv_sec + (double)t.tv_usec * 1e-6;
}

std::streamsize getFileSize(const std::string &filename) {
	ifstream f(filename.c_str(), ios::in | ios::ate);
	return f.tellg();
}

void *getFile(const string &filename, streamsize *dstsize, void *buffer) {
	ifstream f(filename.c_str(), ios::in | ios::binary | ios::ate);
	if (!f.is_open()) throw Exception() << "failed to open file " << filename;
	streamsize size = f.tellg();
	f.seekg(0);
	if (buffer) {
		if (size > *dstsize) {
			delete[] (char*)buffer;
			buffer = NULL;
		}
	}
	if (!buffer) {
		buffer = (void*)(new char[size]);
		if (dstsize) *dstsize = size;
	}
	f.read((char*)buffer, size);
	return buffer;
}

void getFileNameParts(const string &filename, string &base, string &ext) {
	size_t dotpos = filename.find_last_of('.');
	if (dotpos == string::npos) {
		base = filename;
		ext = "";
	} else {
		base = filename.substr(0, dotpos);
		ext = filename.substr(dotpos+1);
	}
}

list<string> getDirFileNames(const string &dir) {
	list<string> destList; 
	
	DIR *dp = NULL;
	struct dirent *pent = NULL;
	char basename[FILENAME_MAX];

	dp = opendir(dir.c_str());
	if (!dp) throw Exception() << "failed to open dir " << dir;
	
	while (pent = readdir(dp)) {
		const char *filename = pent->d_name;
		if (filename[0] != '.') {
			destList.push_back(filename);
		}
	}
	closedir(dp);

	//i hope you have a good copy constructor ...
	return destList;
}

void writeFile(const string &filename, void *data, streamsize size) {
	ofstream f(filename.c_str(), ios::out | ios::binary);
	f.write((const char *)data, size);
}
