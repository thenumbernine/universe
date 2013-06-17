#include <assert.h>
#include <fstream>
#include "exception.h"
#include "writebmp.h"

using namespace std;

void writeImageBMP(char *data, int width, int height, const char *filename) {
	short bitsPerPixel = 24;
	int dataRowSize = (width * bitsPerPixel) >> 3;
	int rowSize = ((dataRowSize >> 2) + !!(dataRowSize & 3)) << 2;
	int rowPadding = rowSize - dataRowSize;
	int offset = 54;
	int imageSize = rowSize * height;
	int filesize = offset + imageSize;
	int zero = 0;
	short planes = 1;
	int compressionMethod = 0;

	ofstream f(filename, ios::out | ios::binary);
	if (f.fail()) throw Exception() << "failed to open file " << filename << " for writing";
	f.write("BM", 2);
	f.write((char*)&filesize, sizeof(filesize));
	f.write((char*)&zero, sizeof(zero));
	f.write((char*)&offset, sizeof(offset));

	int hdrsize = 40;
	f.write((char*)&hdrsize, sizeof(hdrsize));
	f.write((char*)&width, sizeof(width));
	f.write((char*)&height, sizeof(height));
	f.write((char*)&planes, sizeof(planes));
	f.write((char*)&bitsPerPixel, sizeof(bitsPerPixel));
	f.write((char*)&compressionMethod, sizeof(compressionMethod));
	f.write((char*)&imageSize, sizeof(imageSize));
	f.write((char*)&width, sizeof(width));
	f.write((char*)&height, sizeof(height));
	f.write((char*)&zero, sizeof(zero));
	f.write((char*)&zero, sizeof(zero));

	for (int j = 0; j < height; j++) {
		for (int i = 0; i < width; i++) {
			char *rgb = data + (((i + j * width) * bitsPerPixel) >> 3);
			char bgr[3] = {rgb[2], rgb[1], rgb[0]};
			f.write(bgr, 3);
		}
		if (rowPadding) f.write((char*)&zero, rowPadding);
	}
}
