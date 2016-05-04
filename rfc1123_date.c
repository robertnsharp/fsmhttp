#include "rfc1123_date.h"

/* Writes a RFC1123 date to the given buffer, returning the number of
 * chars written. The date does NOT have a trailing carriage return
 * and newline.
 */
int write_rfc1123_date(char *buf, time_t t, size_t maxsize) {

	struct tm *gmt;

	/* convert unix time to GMT time */
	gmt = gmtime(&t);

	return strftime(buf, maxsize, "%a, %d %b %Y %T GMT",
			gmt);
}
