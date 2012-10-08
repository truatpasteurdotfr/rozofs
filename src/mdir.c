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

#define _XOPEN_SOURCE 700
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

#include "mdir.h"

int mdir_open(mdir_t *mdir, const char *path) {

    if ((mdir->fdp = open(path, O_RDONLY | O_NOATIME, S_IRWXU)) < 0) {
        return -1;
    }

    if ((mdir->fdattrs = openat(mdir->fdp, MDIR_ATTRS_FNAME, O_RDWR|O_CREAT, S_IRWXU)) < 0) {
        int xerrno = errno;
        close(mdir->fdp);
        errno = xerrno;
        return -1;
    }

    return 0;
}

void mdir_close(mdir_t *mdir) {
    close(mdir->fdattrs);
    close(mdir->fdp);
}

int mdir_read_attributes(mdir_t *mdir, mattr_t *attrs) {
    // read attributes
    if (pread(mdir->fdattrs, attrs, sizeof(mattr_t), 0) != sizeof(mattr_t)) {
        return -1;
    }
    // read children
    if (pread(mdir->fdattrs, &mdir->children, sizeof(int),
            sizeof(mattr_t)) != sizeof(int)) {
        return -1;
    }

    return 0;
}

int mdir_write_attributes(mdir_t *mdir, mattr_t *attrs) {
    // write attributes
    if (pwrite(mdir->fdattrs, attrs, sizeof(mattr_t), 0) != sizeof(mattr_t)) {
        return -1;
    }
    // write children
    if (pwrite(mdir->fdattrs, &mdir->children, sizeof(int),
            sizeof(mattr_t)) != sizeof(int)) {
        return -1;
    }

    return 0;
}
