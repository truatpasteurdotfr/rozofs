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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <inttypes.h>
#include <glob.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/list.h>
#include <rozofs/common/xmalloc.h>

#include "storage.h"

#define ST_OPEN_FLAG O_RDWR | O_CREAT | O_NOATIME
#define ST_OPEN_MODE S_IFREG | S_IRUSR | S_IWUSR

static char *storage_map(storage_t * st, fid_t fid, tid_t pid, char *path) {
    char str[37];
    DEBUG_FUNCTION;

    strcpy(path, st->root);
    strcat(path, "/");
    uuid_unparse(fid, str);
    strcat(path, str);
    sprintf(str, "-%d.bins", pid);
    strcat(path, str);
    return path;
}


int storage_initialize(storage_t *st, sid_t sid, const char *root) {

    int status = -1;
    struct stat s;

    DEBUG_FUNCTION;

    if (!realpath(root, st->root))
        goto out;
    // sanity checks
    if (stat(st->root, &s) != 0)
        goto out;
    if (!S_ISDIR(s.st_mode)) {
        errno = ENOTDIR;
        goto out;
    }
    st->sid = sid;

    status = 0;
out:
    return status;
}

void storage_release(storage_t * st) {

    DEBUG_FUNCTION;

    st->sid = 0;
    st->root[0] = 0;
}

int storage_write(storage_t * st, fid_t fid, tid_t pid, bid_t bid, uint32_t n,
        size_t len, const bin_t * bins) {

    int status = -1;
    int fd = -1;
    size_t count = 0;
    size_t nb_write = 0;
    char path[FILENAME_MAX];

    DEBUG_FUNCTION;

    storage_map(st, fid, pid, path);
    if ((fd = open(path, ST_OPEN_FLAG, ST_OPEN_MODE)) < 0) {
        severe("%s open failed: %s", path, strerror(errno));
        goto out;
    }

    count = n * rozofs_psizes[pid] * sizeof (bin_t);

    if ((nb_write = pwrite(fd, bins, len,
            (off_t) bid * (off_t)rozofs_psizes[pid] * (off_t) sizeof (bin_t)))
            != count) {
        severe("%s pwrite failed: %s", path, strerror(errno));
        if (nb_write != -1) {
            severe("pwrite failed: only %zu bytes over %zu written", nb_write,
                    count);
            errno = EIO;
        }
        goto out;
    }

    status = 0;
out:
    if (fd != -1) close(fd);
    return status;
}

int storage_read(storage_t * st, fid_t fid, tid_t pid, bid_t bid, uint32_t n,
        bin_t * bins) {
    int status = -1;
    int fd = -1;
    size_t count;
    char path[FILENAME_MAX];

    DEBUG_FUNCTION;

    storage_map(st, fid, pid, path);
    if ((fd = open(path, ST_OPEN_FLAG, ST_OPEN_MODE)) < 0) {
        severe("%s open failed: %s", path, strerror(errno));
        goto out;
    }
    count = n * rozofs_psizes[pid] * sizeof (bin_t);

    if (pread(fd, bins, count,
            (off_t) bid * (off_t) rozofs_psizes[pid] * (off_t) sizeof(bin_t))
            != count) {
        severe("%s pread failed: %s", path, strerror(errno));
        goto out;
    }

    status = 0;
out:
    if (fd != -1) close(fd);
    return status;
}

int storage_truncate(storage_t * st, fid_t fid, tid_t pid, bid_t bid) {
    int status = -1;
    int fd = -1;
    char path[FILENAME_MAX];

    DEBUG_FUNCTION;

    storage_map(st, fid, pid, path);
    if ((fd = open(path, ST_OPEN_FLAG, ST_OPEN_MODE)) < 0) {
        severe("%s open failed: %s", path, strerror(errno));
        goto out;
    }

    status = ftruncate(fd, (bid + 1) * rozofs_psizes[pid] * sizeof (bin_t));
out:
    if (fd != -1) close(fd);
    return status;
}

int storage_rm_file(storage_t * st, fid_t fid) {
    int status = -1;
    char bins_filename[FILENAME_MAX];
    char fid_str[37];
    pid_t pid;

    DEBUG_FUNCTION;

    if (chdir(st->root) != 0) {
        goto out;
    }

    uuid_unparse(fid, fid_str);

    for (pid = 0; pid < rozofs_forward; pid++) {

        if (sprintf(bins_filename, "%36s-%u.bins", fid_str, pid) < 0) {
            severe("storage_rm_file failed: sprintf for bins %s failed: %s",
                    fid_str, strerror(errno));
            goto out;
        }

        if (unlink(bins_filename) == -1) {
            if (errno != ENOENT) {
                severe("storage_rm_file failed: unlink file %s failed: %s",
                        bins_filename, strerror(errno));
                goto out;
            }
        }
    }

    status = 0;
out:
    return status;
}

int storage_stat(storage_t * st, sstat_t * sstat) {
    int status = -1;
    struct statfs sfs;
    DEBUG_FUNCTION;

    if (statfs(st->root, &sfs) == -1)
        goto out;
    sstat->size = (uint64_t) sfs.f_blocks * (uint64_t) sfs.f_bsize;
    sstat->free = (uint64_t) sfs.f_bfree * (uint64_t) sfs.f_bsize;
    status = 0;
out:
    return status;
}
