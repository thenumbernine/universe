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
			buffer = nullptr;
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

HandleArgs::HandleArgs(
	std::vector<std::string> const & args,
	Handlers const & handlers
) {
	showhelp = [=](){
		std::cout << "usage: " << args[0] << "<options>" << std::endl;
		std::cout << "options:" << std::endl;
		for (auto & h : handlers) {
			std::cout << "  " << h.first << " " << h.second.first << std::endl;
		}
	};

	auto nargs = args.size();
	for (int j = 1; j < nargs; ++j) {
		auto i = handlers.find(args[j]);

		auto next = [&](){
			++j;
			if (j >= nargs) {
				throw Exception() << i->first << " expected float";
			}
			return args[j];
		};
		
		auto handle = [&](auto & f){
			std::visit([&](auto && g) {
				using T = std::decay_t<decltype(g)>;
				if constexpr (std::is_same_v<T, std::function<void()>>) {
					g();
				} else if constexpr (std::is_same_v<T, std::function<void(int)>>) {
					g(atoi(next().c_str()));
				} else if constexpr (std::is_same_v<T, std::function<void(float)>>) {
					g(atof(next().c_str()));
				} else if constexpr (std::is_same_v<T, std::function<void(double)>>) {
					g(atof(next().c_str()));
				} else if constexpr (std::is_same_v<T, std::function<void(std::string)>>) {
					g(next());
				}
			}, f);
		};

		//bail on unknown
		//I could make a custom callback
		//but by that point, why are you using this function?
		if (i == handlers.end()) {
			showhelp();
			exit(1);
		} else {
			handle(i->second.second);
		}
	}
}
