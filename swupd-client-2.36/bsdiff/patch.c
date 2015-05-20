#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include "swupd.h"

int main(int argc, char **argv)
{
	int ret;

	if (argc > 4)
		init_log_stdout();
	ret = apply_bsdiff_delta(argv[1], argv[2], argv[3]);
	return ret;
}
