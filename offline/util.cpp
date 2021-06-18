#include <fstream>
#include <filesystem>

#include "util.h"
#include "exception.h"

double profile(const std::string &name, std::function<void()> f) { 
	auto startTime = std::chrono::high_resolution_clock::now();
	f();
	auto endTime = std::chrono::high_resolution_clock::now();
	double deltaTime = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
	std::cout << name << " took " << deltaTime << " seconds" << std::endl;
	return deltaTime;
}

void getlinen(std::ifstream& f, char * const dst, int const dstlen) {
	std::string s;
	std::getline(f, s);
	std::strncpy(dst, s.c_str(), dstlen);
	dst[dstlen-1] = '\0';
}

std::streamsize getFileSize(const std::string &filename) {
	std::ifstream f(filename.c_str(), std::ios::in | std::ios::ate);
	return f.tellg();
}

void *getFile(const std::string &filename, std::streamsize *dstsize, void *buffer) {
	std::ifstream f(filename.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
	if (!f.is_open()) throw Exception() << "failed to open file " << filename;
	std::streamsize size = f.tellg();
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

void getFileNameParts(const std::string &filename, std::string &base, std::string &ext) {
	size_t dotpos = filename.find_last_of('.');
	if (dotpos == std::string::npos) {
		base = filename;
		ext = "";
	} else {
		base = filename.substr(0, dotpos);
		ext = filename.substr(dotpos+1);
	}
}

std::list<std::string> getDirFileNames(std::string const & dir) {
	std::list<std::string> destList; 
	for (auto& p : std::filesystem::directory_iterator(dir)) {
		destList.push_back(p.path().filename().string());
	}
	return destList;
}

void writeFile(const std::string &filename, void *data, std::streamsize size) {
	std::ofstream f(filename.c_str(), std::ios::out | std::ios::binary);
	f.write((const char *)data, size);
}
