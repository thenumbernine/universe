#pragma once

#include "exception.h"
#include <string>
#include "fitsio.h"

std::string fitsGetError(int status);

template<typename Func, typename ... Args>
void fitsSafe(Func f, Args && ... args) {
	int status = 0;
	f(std::forward<Args>(args)..., &status);
	if (status) throw Exception() << fitsGetError(status);
}

template<typename T> constexpr int fitsType;
template<> constexpr int fitsType<bool> = TBYTE;
template<> constexpr int fitsType<short> = TSHORT;
template<> constexpr int fitsType<int> = TINT32BIT;
template<> constexpr int fitsType<long> = TLONG;
template<> constexpr int fitsType<long long> = TLONGLONG;
template<> constexpr int fitsType<float> = TFLOAT;
template<> constexpr int fitsType<double> = TDOUBLE;
template<> constexpr int fitsType<std::string> = TSTRING;

struct FITSColumn {
	fitsfile *file;
	const char *colName;
	int colNum;
	
	FITSColumn(fitsfile *file_, const char *colName_) 
	: file(file_), colName(colName_), colNum(0) {}

	virtual std::string readStr(int rowNum) = 0;
};

template<typename CTYPE_>
struct FITSTypedColumn : public FITSColumn {
	typedef CTYPE_ CTYPE;
	FITSTypedColumn(fitsfile *file_, const char *colName_) 
	: FITSColumn(file_, colName_) {
		fitsSafe(fits_get_colnum, file, CASESEN, const_cast<char*>(colName), &colNum);
		assertColType(file, colNum);
	}

	void assertColType(fitsfile *file, int colNum) {
		int colType = 0;
		long repeat = 0;
		long width = 0;
		fitsSafe(fits_get_coltype, file, colNum, &colType, &repeat, &width);
		//std::cout << "type " << colType << " vs TDOUBLE " << TDOUBLE << " vs TFLOAT " << TFLOAT << std::endl;
		if (colType != fitsType<CTYPE>) throw Exception() << "for column " << colNum << " expected FITS type " << (int)fitsType<CTYPE> << " but found " << colType;
		if (repeat != 1) throw Exception() << "for column " << colNum << " expected repeat to be 1 but found " << repeat;
		if (width != sizeof(CTYPE)) throw Exception() << "for column " << colNum << " expected column width to be " << sizeof(CTYPE) << " but found " << width;
	}

	CTYPE read(int rowNum) {
		CTYPE result = CTYPE();
		CTYPE nullValue = CTYPE();
		int nullResult = 0;
		fitsSafe(fits_read_col, file, fitsType<CTYPE>, colNum, rowNum, 1, 1, &nullValue, &result, &nullResult);
		if (nullResult != 0) throw Exception() << "got nullResult " << nullResult;
		return result;
	}
	
	virtual std::string readStr(int rowNum) {
		std::ostringstream ss;
		ss << read(rowNum);
		return ss.str();
	}
};

struct FITSStringColumn : public FITSColumn {
	typedef std::string CTYPE;
	long width;
	FITSStringColumn(fitsfile *file_, const char *colName_)
	: FITSColumn(file_, colName_) {
		fitsSafe(fits_get_colnum, file, CASESEN, const_cast<char*>(colName), &colNum);
		
		int colType = 0;
		long repeat = 0;
		fitsSafe(fits_get_coltype, file, colNum, &colType, &repeat, &width);
		if (colType != fitsType<CTYPE>) throw Exception() << "expected FITS type " << (int)fitsType<CTYPE> << " but found " << colType;
		if (width != repeat * sizeof(char)) throw Exception() << "expected column width to be " << (repeat * sizeof(char)) << " but found " << width;
	}

	CTYPE read(int rowNum) {
		char buffer[256];
		char *result = buffer;
		if (width >= sizeof(buffer)) throw Exception() << "found column that won't fit: needs to be " << width << " bytes";
		
		int nullResult = 0;
	
		fitsSafe(fits_read_col, file, fitsType<CTYPE>, colNum, rowNum, 1, 1, nullptr, &result, &nullResult);
		
		if (nullResult != 0) throw Exception() << "got nullResult " << nullResult;
		return std::string(buffer);

	}
	
	virtual std::string readStr(int rowNum) {
		return read(rowNum);
	}
};
