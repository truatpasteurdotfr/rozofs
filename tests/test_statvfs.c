/*
 Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
 This file is part of Rozofs.

 Rozofs is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published
 by the Free Software Foundation, version 2.

 Rozofs is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see
 <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <getopt.h>
#include <inttypes.h>
#include <sys/statvfs.h>

static char mountpoint[PATH_MAX] = "mnt1_1";

void usage() {
	printf("Usage: test_fstat [OPTIONS]\n\n");
	printf("   -h, --help\t\t\tprint this message.\n");
	printf("   -m, --mountpoint=directory\t"
			"specify the mountpoint used for test (default: none).\n");
}

int main(int argc, char *argv[]) {

	int c = 0;
	static struct option long_options[] = {
			{ "help", no_argument, 0, 'h' }, {
			"mountpoint", required_argument, 0, 'm' },
			{ 0, 0, 0, 0 }
	};

	while (1) {

		int option_index = 0;
		c = getopt_long(argc, argv, "hm:", long_options, &option_index);

		if (c == -1)
			break;

		switch (c) {

		case 'h':
			usage();
			exit(EXIT_SUCCESS);
			break;
		case 'm':
			if (!realpath(optarg, mountpoint)) {
				fprintf(stderr, "test_fstat failed: %s %s\n", optarg,
						strerror(errno));
				exit(EXIT_FAILURE);
			}
			break;
		case '?':
			usage();
			exit(EXIT_SUCCESS);
			break;
		default:
			usage();
			exit(EXIT_FAILURE);
			break;
		}
	}

	struct statvfs st;

	if (statvfs(mountpoint, &st) != 0) {
		fprintf(stderr, "statvfs failed (mountpoint: %s): %s.\n", mountpoint,
				strerror(errno));
		goto error;
	}

	/* optimal transfer block size */
	fprintf(stdout, "f_bsize: %lu \n", st.f_bsize);
	/* fragment size (since Linux 2.6) */
	fprintf(stdout, "f_frsize: %lu \n", st.f_frsize);
	/* total data blocks in file system */
	fprintf(stdout, "f_blocks: %lu \n", st.f_blocks);
	/* free blocks in fs */
	fprintf(stdout, "f_bfree: %lu \n", st.f_bfree);
	/* free blocks available to unprivileged user */
	fprintf(stdout, "f_bavail: %lu \n", st.f_bavail);
	/* total file nodes in file system */
	fprintf(stdout, "f_files: %lu \n", st.f_files);
	/* free file nodes in fs */
	fprintf(stdout, "f_ffree: %lu \n", st.f_ffree);
	/* maximum length of filenames */
	fprintf(stdout, "f_namemax: %lu \n", st.f_namemax);
	/* file system id */
	fprintf(stdout, "f_fsid: %lu \n", st.f_fsid);

	exit(0);

error:
	fprintf(stderr, "Test failed\n");
	exit(EXIT_FAILURE);
}
