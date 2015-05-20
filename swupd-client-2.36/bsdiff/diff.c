#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "swupd.h"
#include "swupd_bsdiff.h"


/* parse encoding as string and return value as enum
 */

int get_encoding(char * encoding)
{
	if (strcmp(encoding, "raw") == 0)
		return BSDIFF_ENC_NONE;
	else if (strcmp(encoding, "bzip2") == 0)
		return BSDIFF_ENC_BZIP2;
	else if (strcmp(encoding, "gzip") == 0)
		return BSDIFF_ENC_GZIP;
	else if (strcmp(encoding, "xz") == 0)
		return BSDIFF_ENC_XZ;
	else if (strcmp(encoding, "zeros") == 0)
		return BSDIFF_ENC_ZEROS;
	else if (strcmp(encoding, "any") == 0)
		return BSDIFF_ENC_ANY;
	else
		return -1;
}

/* creates a bsdiff delta patchfile
 *
 * USAGE:
 *    bsdiff original_file altered_file patchfile [ encoding ] [ "debug" ]
 * 	  where 'encoding' could be 'raw', 'bzip2', 'gzip', 'xz', 'zeros' or 'any'
 *    to activate debug, encoding parameter become mandatory (due to simple options parsing)
 *    and the value must be the string "debug"
 * eg.: bsdiff original_file altered_file patchfile "raw" "debug"
 *
 * - invoking with "raw" will force no compression
 * - additionally invoking with "debug" will log actions to stdout
 */
int main(int argc, char **argv)
{
	int ret, enc = BSDIFF_ENC_ANY;

	if (argc > 4) {
		if ((enc = get_encoding(argv[4])) < 0) {
			printf("Unknown encoding algorithm");
			return -1;
		}
	}
	if (argc > 5) {
		if (strcmp(argv[5], "debug") == 0)
			init_log_stdout();
	}

	printf("Creating delta\n");
	ret = make_bsdiff_delta(argv[1], argv[2], argv[3], enc);
	printf("Done\n");
	printf("ret is %i \n", ret);
	return ret;
}
