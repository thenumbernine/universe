#pragma once

#include <fstream>
#include <chrono>
#include <functional>
#include <algorithm>
#include <list>
#include <map>
#include <vector>
#include <functional>
#include <string>
#include <fstream>
#include <iostream>
#include "macros.h"

double profile(const std::string &name, std::function<void()> f);

void getlinen(std::ifstream& f, char * const dst, int const dstlen);

std::streamsize getFileSize(const std::string &filename);
/*
usage: 
getFile(filename) = returns the file, size unknown
getFile(filename, &size) = returns the file, writes the size into 'size'
getFile(filename, &size, ptr) = uses the 'ptr' buffer if size >= filesize, otherwise allocates new
*/
void *getFile(const std::string &filename, std::streamsize *size = NULL, void *buffer = NULL);

void getFileNameParts(const std::string &filename, std::string &base, std::string &ext);

std::list<std::string> getDirFileNames(std::string const & dir);

//eh seems like it would be useful if it didn't take a callback function ...
template<typename IteratedType, typename Function>
void for_all(IteratedType &i, Function &f) {
	std::for_each(i.begin(), i.end(), f);
}

void writeFile(const std::string &filename, void *data, std::streamsize size);

#include <variant>

using Handlers = std::map<
	std::string,
	std::pair<
		std::string,
		std::variant<
			std::function<void()>,
			std::function<void(int)>,
			std::function<void(float)>,
			std::function<void(std::string)>
		>
	>
>;

// put varying # of args on the function, and parse & skip those in the handler section
struct HandleArgs {
	HandleArgs(
		std::vector<std::string> const & args,
		Handlers const & handlers
	);

	std::function<void()> showhelp;
};
