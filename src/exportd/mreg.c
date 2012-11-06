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

#define _XOPEN_SOURCE 500
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <rozofs/common/profile.h>
#include <rozofs/rpc/epproto.h>

#include "mreg.h"

DECLARE_PROFILING(epp_profiler_t);

int mreg_open(mreg_t *mreg, const char *path) {
    START_PROFILING(mreg_open);

    mreg->fdattrs = open(path, O_RDWR | O_NOATIME, S_IRWXU);

    STOP_PROFILING(mreg_open);
    return mreg->fdattrs < 0 ? -1 : 0;
}

void mreg_close(mreg_t *mreg) {
    START_PROFILING(mreg_close);

    close(mreg->fdattrs);

    STOP_PROFILING(mreg_close);
}

int mreg_read_attributes(mreg_t *mreg, mattr_t *attrs) {
    int status;

    START_PROFILING(mreg_read_attributes);

    status = pread(mreg->fdattrs, attrs, sizeof(mattr_t), 0)
            == sizeof(mattr_t) ? 0 : -1;

    STOP_PROFILING(mreg_read_attributes);

    return status;
}

int mreg_write_attributes(mreg_t *mreg, mattr_t *attrs) {
    int status;

    START_PROFILING(mreg_write_attributes);

    status = pwrite(mreg->fdattrs, attrs, sizeof(mattr_t), 0)
            == sizeof(mattr_t) ? 0 : -1;

    STOP_PROFILING(mreg_write_attributes);

    return status;
}

int mreg_read_dist(mreg_t *mreg, bid_t bid, uint32_t n, dist_t *dist) {
    int status;
    off_t off = 0;
    ssize_t len = 0;

    START_PROFILING(mreg_read_dist);

    off = sizeof(mattr_t) + bid * sizeof(dist_t);
    len = n * sizeof(dist_t);

    status = pread(mreg->fdattrs, dist, len, off) == len ? 0 : -1;

    STOP_PROFILING(mreg_read_dist);

    return status;
}

int mreg_write_dist(mreg_t *mreg, bid_t bid, uint32_t n, dist_t *dist) {
    int status;
    off_t off = 0;
    ssize_t len = 0;

    START_PROFILING(mreg_write_dist);

    off = sizeof(mattr_t) + bid * sizeof(dist_t);
    len = n * sizeof(dist_t);

    status = pwrite(mreg->fdattrs, dist, len, off) == len ? 0 : -1;

    STOP_PROFILING(mreg_write_dist);

    return status;
}
