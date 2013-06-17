#ifndef WRITEBMP_H
#define WRITEBMP_H

/*
assumes a 24bpp bytestream
*/
void writeImageBMP(char *data, int width, int height, const char *filename);

#endif	//WRITEBMP_H
