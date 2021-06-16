#pragma once

#include <chrono>
#include <functional>
#include <algorithm>
#include <list>
#include <string>
#include <fstream>
#include <iostream>

bool fileexists(const std::string &filename);

double profile(const std::string &name, std::function<void()> f) { 
	auto startTime = std::chrono::high_resolution_clock::now();
	f();
	auto endTime = std::chrono::high_resolution_clock::now();
	double deltaTime = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
	std::cout << name << " took " << deltaTime << " seconds" << std::endl;
	return deltaTime;
}

std::streamsize getFileSize(const std::string &filename);
/*
usage: 
getFile(filename) = returns the file, size unknown
getFile(filename, &size) = returns the file, writes the size into 'size'
getFile(filename, &size, ptr) = uses the 'ptr' buffer if size >= filesize, otherwise allocates new
*/
void *getFile(const std::string &filename, std::streamsize *size = NULL, void *buffer = NULL);

void getFileNameParts(const std::string &filename, std::string &base, std::string &ext);

std::list<std::string> getDirFileNames(const std::string &dir);

//eh seems like it would be useful if it didn't take a callback function ...
template<typename IteratedType, typename Function>
void for_all(IteratedType &i, Function &f) {
	std::for_each(i.begin(), i.end(), f);
}

void writeFile(const std::string &filename, void *data, std::streamsize size);
