/*
does the same thing as show.cpp, give or take, but all offline rendering
*/
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <vector>
#include "vec.h"
#include "exception.h"
#include "util.h"
#include "writebmp.h"

using namespace std;
void showhelp() {
	cout
	<< "usage: show <options>" << endl
	<< "options:" << endl
	<< "	--set <set>			specify the dataset. default is 'allsky'." << endl
	<< "	--file	<file> add that file." << endl
	<< "	--all	use all files." << endl
	<< "	--lum <lum>		how much to scale luminosity." << endl
	;
}

void handleArgs(int argc, char **argv, vector<string> &filenames, string &datasetname, double &lumScale) {
	datasetname = "allsky";
	bool gotDir = false;
	for (int k = 1; k < argc; k++) {
		if (!strcmp(argv[k], "--set") && k < argc-1) {
			datasetname = argv[++k];
		} else if (!strcmp(argv[k], "--all")) {
			gotDir = true;
		} else if (!strcmp(argv[k], "--file") && k < argc-1) {
			filenames.push_back(argv[++k]);
		} else if (!strcmp(argv[k], "--lum") && k < argc-1) {
			lumScale = atof(argv[++k]);
		} else {
			showhelp();
			exit(0);
		}
	}

	if (gotDir) {
		string dirname = string("datasets/") + datasetname + "/points";
		list<string> dirFilenames = getDirFileNames(dirname);
		for (list<string>::iterator i = dirFilenames.begin(); i != dirFilenames.end(); ++i) {
			const string &filename = *i;
			string base, ext;
			getFileNameParts(filename, base, ext);
			if (ext == "f32") {
				filenames.push_back(dirname + "/" + filename);
			}
		}
	}

	if (!filenames.size()) {
		cout << "got no files!" << endl;
		showhelp();
		exit(0);
	}
}

float psf(int dx, int dy, float f) {
	return f * exp(-(float)(dx*dx + dy*dy));
}

int main(int argc, char **argv) {
	//TODO accumulate all points into a framebuffer
	//inverse square diminish intensity
	//THEN apply PSF
	//and finally write out the image
	double lumScale = 1.;
	vector<string> filenames;
	string datasetname;
	handleArgs(argc, argv, filenames, datasetname, lumScale);

cout << "allocating image buffer" << endl;
	int screenWidth = 2048;
	int screenHeight = 1024;
	float aspectRatio = (float)screenWidth / (float)screenHeight;
	float *screenBuffer = new float[screenWidth * screenHeight];
	memset(screenBuffer, 0, sizeof(float) * screenWidth * screenHeight);

cout << "accumulating stars" << endl;
	for (vector<string>::iterator i = filenames.begin(); i != filenames.end(); ++i) {
		const string &filename = *i;
		streamsize vtxCount = 0;
		vec3f *vtxBuffer = (vec3f*)getFile(filename, &vtxCount);
		vtxCount /= sizeof(vec3f);
		vec3f *vtxEnd = vtxBuffer + vtxCount;
		for (vec3f *_v = vtxBuffer; _v < vtxEnd; _v++) {
			//now inverse transform (if you want)
			vec3f v = *_v;
			float d = -v.z;
			if (d < 0) continue;
			v.x /= d; 
			v.y /= d; 
			//TODO scale by fov if you want something other than 90' on the y
			v.x *= aspectRatio;
			if (v.x < 0 || v.x > 1 || v.y < 0 || v.y > 1) continue;
			v.x *= (float)(screenWidth-1);
			v.y *= (float)(screenHeight-1);
			int x = (int)v.x;
			int y = (int)v.y;
			float lum = 1. / (d * d);
			screenBuffer[x+screenWidth*y] += lum * lumScale;
		}
	}

	//TODO write out screenBuffer

cout << "applying psf" << endl;
	float *psfBuffer = new float[screenWidth * screenHeight];
	memset(psfBuffer, 0, sizeof(float) * screenWidth * screenHeight);

	const int r = 5;
	for (int y = 0;  y < screenHeight; y++) {
		for (int x = 0; x < screenWidth; x++) {
			float intensity = 0.;
			int umin = x - r;
			int umax = x + r;
			int vmin = y - r;
			int vmax = y + r;
			if (umin < 0) umin = 0;
			if (umax >= screenWidth) umax = screenWidth-1;
			if (vmin < 0) vmin = 0;
			if (vmax >= screenHeight) vmax = screenHeight-1;
			for (int u = umin; u <= umax; u++) {
				for (int v = vmin; v <= vmax; v++) {
					intensity += psf(u-x, v-y, screenBuffer[u+screenWidth*v]);
				}
			}
			psfBuffer[x+screenWidth*y] = intensity;
		}
	}

	//TODO write out psfBuffer
cout << "creating rgb buffer" << endl;
	unsigned char *writeBuffer = new unsigned char[screenWidth * screenHeight * 3];
	{
		float *src = psfBuffer;
		unsigned char *dst = writeBuffer;
		for (int y = 0; y < screenHeight; y++) {
			for (int x = 0; x < screenWidth; x++) {
				unsigned char l = (unsigned char)(255. * src[0]);
				src++;
				*dst = l; dst++;
				*dst = l; dst++;
				*dst = l; dst++;
			}
		}
	}

cout << "writing file" << endl;
	writeImageBMP((char*)writeBuffer, screenWidth, screenHeight, "still.bmp");
}

