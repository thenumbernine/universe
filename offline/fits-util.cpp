#include "fits-util.h"

//fits is rigid and I am lazy.  use its writer to stderr and return the same status
std::string fitsGetError(int status) {
	//let the default stderr writer do its thing
	fits_report_error(stderr, status);
	//then capture the 30-char-max error
	char buffer[32];
	memset(buffer, 0, sizeof(buffer));
	fits_get_errstatus(status, buffer);
	std::ostringstream ss;
	ss << "FITS error " << status << ": " << buffer;
	return ss.str();
}
