/*
 Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
 This file is part of Rozofs.

 Rozofs is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published
 by the Free Software Foundation; either version 3 of the License,
 or (at your option) any later version.

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

#include "mreg.h"

int mreg_open(mreg_t *mreg, const char *path) {
    mreg->fdattrs = open(path, O_RDWR | O_NOATIME, S_IRWXU);
    return mreg->fdattrs < 0 ? -1 : 0;
}

void mreg_close(mreg_t *mreg) {
    close(mreg->fdattrs);
}

int mreg_read_attributes(mreg_t *mreg, mattr_t *attrs) {
    return pread(mreg->fdattrs, attrs, sizeof(mattr_t), 0)
            == sizeof(mattr_t) ? 0 : -1;
}

int mreg_write_attributes(mreg_t *mreg, mattr_t *attrs) {
    return pwrite(mreg->fdattrs, attrs, sizeof(mattr_t), 0)
            == sizeof(mattr_t) ? 0 : -1;
}

int mreg_read_dist(mreg_t *mreg, bid_t bid, uint32_t n, dist_t *dist) {
    off_t off = 0;
    ssize_t len = 0;

    off = sizeof(mattr_t) + bid * sizeof(dist_t);
    len = n * sizeof(dist_t);

    return pread(mreg->fdattrs, dist, len, off) == len ? 0 : -1;
}

int mreg_write_dist(mreg_t *mreg, bid_t bid, uint32_t n, dist_t *dist) {
    off_t off = 0;
    ssize_t len = 0;

    off = sizeof(mattr_t) + bid * sizeof(dist_t);
    len = n * sizeof(dist_t);

    return pwrite(mreg->fdattrs, dist, len, off) == len ? 0 : -1;
}
