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
#include <rozofs/rozofs_srv.h>

#include "storage.h"

static char *storage_map_distribution(storage_t * st, uint8_t layout,
        sid_t dist_set[ROZOFS_SAFE_MAX], uint8_t spare, char *path) {
    int i = 0;
    char build_path[FILENAME_MAX];

    DEBUG_FUNCTION;

    strcpy(path, st->root);
    strcat(path, "/");
    sprintf(build_path, "layout_%u/spare_%u/", layout, spare);
    strcat(path, build_path);
    uint8_t rozofs_safe = rozofs_get_rozofs_safe(layout);

    for (i = 0; i < rozofs_safe; i++) {
        char build_path_2[FILENAME_MAX];
        sprintf(build_path_2, "%.3u", dist_set[i]);
        strcat(path, build_path_2);
        if (i != (rozofs_safe - 1))
            strcat(path, "-");
    }
    strcat(path, "/");
    return path;
}

/*
 ** Build the path for the projection file
  @param fid: unique file identifier
  @param path : pointer to the buffer where reuslting path will be stored
  
  @retval pointer to the beginning of the path
  
 */
static char *storage_map_projection(fid_t fid, char *path) {
    char str[37];

    uuid_unparse(fid, str);
    strcat(path, str);
    sprintf(str, ".bins");
    strcat(path, str);
    return path;
}

int storage_initialize(storage_t *st, sid_t sid, const char *root) {
    int status = -1;
    uint8_t layout = 0;
    char path[FILENAME_MAX];
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

    // Build directories for each possible layout if necessary
    for (layout = 0; layout < LAYOUT_MAX; layout++) {

        // Build layout level directory
        sprintf(path, "%s/layout_%u", st->root, layout);
        if (access(path, F_OK) == -1) {
            if (errno == ENOENT) {
                // If the directory doesn't exist, create it
                if (mkdir(path, ROZOFS_ST_DIR_MODE) != 0)
                    goto out;
            } else {
                goto out;
            }
        }

        // Build spare level directories
        sprintf(path, "%s/layout_%u/spare_0", st->root, layout);
        if (access(path, F_OK) == -1) {
            if (errno == ENOENT) {
                // If the directory doesn't exist, create it
                if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) != 0)
                    goto out;
            } else {
                goto out;
            }
        }
        sprintf(path, "%s/layout_%u/spare_1", st->root, layout);
        if (access(path, F_OK) == -1) {
            if (errno == ENOENT) {
                // If the directory doesn't exist, create it
                if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) != 0)
                    goto out;
            } else {
                goto out;
            }
        }
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

int storage_write(storage_t * st, uint8_t layout, sid_t * dist_set,
        uint8_t spare, fid_t fid, bid_t bid, uint32_t nb_proj, uint8_t version,
        const bin_t * bins) {
    int status = -1;
    char path[FILENAME_MAX];
    int fd = -1;
    size_t nb_write = 0;
    size_t length_to_write = 0;
    off_t bins_file_offset = 0;
    uint16_t rozofs_max_psize = 0;
    uint8_t write_file_hdr = 0;

    // Build the full path of directory that contains the bins file
    storage_map_distribution(st, layout, dist_set, spare, path);

    // Check that this directory already exists, otherwise it will be create
    if (access(path, F_OK) == -1) {
        if (errno == ENOENT) {
            // If the directory doesn't exist, create it
            if (mkdir(path, ROZOFS_ST_DIR_MODE) != 0) {
                severe("mkdir failed (%s) : %s", path, strerror(errno));
                goto out;
            }
        } else {
            goto out;
        }
    }

    // Build the path of bins file
    storage_map_projection(fid, path);

    // Check that this file already exists
    if (access(path, F_OK) == -1)
        write_file_hdr = 1; // We must write the header

    // Open bins file
    fd = open(path, ROZOFS_ST_BINS_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
    if (fd < 0) {
        severe("open failed (%s) : %s", path, strerror(errno));
        goto out;
    }

    // If we write the bins file for the first time, we must write the header
    if (write_file_hdr) {
        // Prepare file header
        rozofs_stor_bins_file_hdr_t file_hdr;
        memcpy(file_hdr.dist_set_current, dist_set,
                ROZOFS_SAFE_MAX * sizeof (sid_t));
        memset(file_hdr.dist_set_next, 0, ROZOFS_SAFE_MAX * sizeof (sid_t));
        file_hdr.layout = layout;
        file_hdr.version = version;

        // Write the header for this bins file
        nb_write = pwrite(fd, &file_hdr, sizeof (rozofs_stor_bins_hdr_t), 0);
        if (nb_write != sizeof (rozofs_stor_bins_hdr_t)) {
            severe("pwrite failed: %s", strerror(errno));
            goto out;
        }
    }

    // Compute the offset and length to write
    rozofs_max_psize = rozofs_get_max_psize(layout);
    bins_file_offset = ROZOFS_ST_BINS_FILE_HDR_SIZE + bid * (rozofs_max_psize *
            sizeof (bin_t) + sizeof (rozofs_stor_bins_hdr_t));
    length_to_write = nb_proj * (rozofs_max_psize * sizeof (bin_t)
            + sizeof (rozofs_stor_bins_hdr_t));

    // Write nb_proj * (projection + header)
    nb_write = pwrite(fd, bins, length_to_write, bins_file_offset);
    if (nb_write != length_to_write) {
        severe("pwrite failed: %s", strerror(errno));
        goto out;
    }

    // Write is successful
    status = 0;

out:
    if (fd != -1) close(fd);
    return status;
}

int storage_read(storage_t * st, uint8_t layout, sid_t * dist_set,
        uint8_t spare, fid_t fid, bid_t bid, uint32_t nb_proj,
        bin_t * bins, size_t * len_read) {

    int status = -1;
    char path[FILENAME_MAX];
    int fd = -1;
    size_t nb_read = 0;
    size_t length_to_read = 0;
    off_t bins_file_offset = 0;
    uint16_t rozofs_max_psize = 0;

    // Build the full path of directory that contains the bins file
    storage_map_distribution(st, layout, dist_set, spare, path);

    // Build the path of bins file
    storage_map_projection(fid, path);

    // Check that this file already exists
    if (access(path, F_OK) == -1)
        goto out;

    // Open bins file
    fd = open(path, ROZOFS_ST_BINS_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
    if (fd < 0) {
        severe("open failed (%s) : %s", path, strerror(errno));
        goto out;
    }

    // Compute the offset and length to read
    rozofs_max_psize = rozofs_get_max_psize(layout);
    bins_file_offset = ROZOFS_ST_BINS_FILE_HDR_SIZE +
            bid * ((off_t) (rozofs_max_psize * sizeof (bin_t)) +
            sizeof (rozofs_stor_bins_hdr_t));
    length_to_read = nb_proj * (rozofs_max_psize * sizeof (bin_t)
            + sizeof (rozofs_stor_bins_hdr_t));

    // Read nb_proj * (projection + header)
    nb_read = pread(fd, bins, length_to_read, bins_file_offset);

    // Check the length read
    if ((nb_read % (rozofs_max_psize * sizeof (bin_t) + sizeof (rozofs_stor_bins_hdr_t))) != 0) {
        severe("pread failed: %s", strerror(errno));
        goto out;
    }

    // Update the length read
    *len_read = nb_read;

    // Read is successful
    status = 0;

out:
    if (fd != -1) close(fd);
    return status;
}
// XXX Not used
int storage_truncate(storage_t * st, uint8_t layout, sid_t * dist_set,
        uint8_t spare, fid_t fid, tid_t proj_id, bid_t bid) {
    int status = -1;
    int fd = -1;
    char path[FILENAME_MAX];

    DEBUG_FUNCTION;

    // Build the full path of directory that contains the bins file
    storage_map_distribution(st, layout, dist_set, spare, path);

    // Build the path of bins file
    storage_map_projection(fid, path);

    // Check that this file already exists
    if (access(path, F_OK) == -1)
        goto out;

    // Open bins file
    fd = open(path, ROZOFS_ST_BINS_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
    if (fd < 0) {
        severe("open failed (%s) : %s", path, strerror(errno));
        goto out;
    }

    status = ftruncate(fd, (bid + 1) * rozofs_get_psizes(layout, proj_id)
            * sizeof (bin_t));
out:
    if (fd != -1) close(fd);
    return status;
}

int storage_rm_file(storage_t * st, uint8_t layout, sid_t * dist_set,
        fid_t fid) {
    int status = -1;
    uint8_t spare = 0;
    char path[FILENAME_MAX];

    DEBUG_FUNCTION;

    // For spare and no spare
    for (spare = 0; spare < 2; spare++) {

        // Build the full path of directory that contains the bins file
        storage_map_distribution(st, layout, dist_set, spare, path);

        // Build the path of bins file
        storage_map_projection(fid, path);

        // Check that this file exists
        if (access(path, F_OK) == -1)
            continue;

        if (unlink(path) == -1) {
            if (errno != ENOENT) {
                severe("storage_rm_file failed: unlink file %s failed: %s",
                        path, strerror(errno));
                goto out;
            }
        } else {
            // It's not possible for one storage to store one bins file
            // in directories spare and no spare.
            status = 0;
            goto out;
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