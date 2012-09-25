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
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <dirent.h>

#include "log.h"
#include "rozofs.h"
#include "xmalloc.h"
#include "mdirent_vers1.h"
#include "mdir.h"

/**
 *  Hash function for a name of mdirentry
 * 
 * @param *key: pointer to the name of the mdirentry
 * 
 * @retval hash_value
 */
static inline uint32_t hash_name(void *key) {
    uint32_t hash = 0;
    char *d;

    for (d = key; *d != '\0'; d++)
        hash = *d + (hash << 6) + (hash << 16) - hash;

    return (hash % MAX_LV3_BUCKETS);
}

/**
 *  Build the absolute LVL3_DIRENT name 
 *  That API uses the hash value (index) of the mdirents file
 *  and build the LVL3_DIRENT name
 * 
 * @param *pathname: pointer to the name of the file or directory
 * @param hash_idx: hash value
 * 
 * @retval *pathname: pointer to the first byte of the pathname
 */
static inline char *build_path_lvl3_mdirents_file_root(char *pathname, uint32_t hash_idx) {

    sprintf(pathname, "dirent_%u", hash_idx);

    return pathname;
}

/**
 * Build the absolute LVL3_DIRENT_COLL name
 * That API uses the generate fid of the mdirents file
 * and build the LVL3_DIRENT name
 * 
 * @param *pathname: pointer to the result pathname
 * @param fid_coll: unique identifier of the mdirents collision file
 * 
 * @retval *pathname: pointer to the first byte of the pathname
 */
static inline char *build_path_lvl3_mdirents_file_coll(char *pathname, fid_t fid_coll) {
    char fid_str[37];
    uuid_unparse(fid_coll, fid_str);
    sprintf(pathname, "coll_%s", fid_str);

    return pathname;
}

/**
 * Build the path and write the mdirents file on disk
 * 
 * @param dirfd: file descriptor of the parent directory
 * @param *mdirents_file_p: pointer to the mdirents file (memory)
 * 
 * @retval  0 on success
 * @retval -1 on failure
 */
static inline int write_mdirents_file(int dirfd, mdirents_file_t *mdirents_file_p) {
    int status = -1;
    int fd = -1;
    char pathname[ROZOFS_FILENAME_MAX];
    int flag = O_WRONLY | O_CREAT;

    if (mdirents_file_p->header.mdirents_type == MDIRENTS_FILE_ROOT_TYPE) {
        build_path_lvl3_mdirents_file_root(pathname, mdirents_file_p->header.fid_cur.link_ref.hash_name_idx);
    } else {
        build_path_lvl3_mdirents_file_coll(pathname, mdirents_file_p->header.fid_cur.link_ref.fid);
    }

    if ((fd = openat(dirfd, pathname, flag, S_IRWXU)) == -1)
        goto out;

    if (pwrite(fd, mdirents_file_p, sizeof (mdirents_file_t), 0) != (sizeof (mdirents_file_t))) {
        severe("pwrite failed in file %s: %s", pathname, strerror(errno));
        goto out;
    }

    status = 0;
out:
    if (fd != -1)
        close(fd);
    return status;
}

/**
 * Read the mdirents file on disk
 * 
 * @param dirfd: file descriptor of the parent directory
 * @param *pathname: pointer to the pathname to read
 * 
 * @retval NULL if this mdirents file doesn't exist
 * @retval pointer to the mdirents file
 */
static inline mdirents_file_t * read_mdirents_file(int dirfd, char *pathname) {
    int fd = -1;
    int flag = O_RDONLY;
    mdirents_file_t * dirent_p = NULL;

    if ((fd = openat(dirfd, pathname, flag, S_IRWXU)) == -1)
        goto out;

    dirent_p = xmalloc(sizeof (mdirents_file_t));
    memset(dirent_p, 0, sizeof (mdirents_file_t));


    if (pread(fd, dirent_p, sizeof (mdirents_file_t), 0) != sizeof (mdirents_file_t)) {
        severe("pread failed in file %s: %s", pathname, strerror(errno));
        free(dirent_p);
        dirent_p = NULL;
        goto out;
    }

out:
    if (fd != -1)
        close(fd);
    return dirent_p;
}

/**
 * API to provide the content of the next mdirents file
 *
 * @param dirfd: file descriptor of the parent directory
 * @param *mdirents_file_p: pointer to the current mdirents file
 *
 * @retval NULL if there no more mdirents file
 * @retval pointer to the next mdirents file
 */
static inline mdirents_file_t * get_next_mdirents_file(int dirfd, mdirents_file_t * mdirents_file_p) {
    mdirents_header_t header_p;
    mdirents_file_t * next_coll_mdirents_file_p = NULL;

    header_p = mdirents_file_p->header;

    // Check if the next file is the root mdirents file
    if (header_p.fid_next.link_type == MDIRENTS_FILE_ROOT_TYPE) {
        // Reloop on the head, so exit
        return NULL;
    }

    // Here is the case of a collision mdirentry
    // so open and return the next mdirents file

    char pathname[ROZOFS_FILENAME_MAX];
    build_path_lvl3_mdirents_file_coll(pathname, header_p.fid_next.link_ref.fid);


    if ((next_coll_mdirents_file_p = read_mdirents_file(dirfd, pathname)) == NULL) {
        fatal("read_mdirents_file failed: %s", strerror(errno));
    }

    return next_coll_mdirents_file_p;
}

/**
 * Build the path and delete the mdirents file on disk
 * 
 * @param dirfd: file descriptor of the parent directory
 * @param *mdirents_file_p: pointer to the mdirents file (memory)
 * 
 * @retval  0 on success
 * @retval -1 on failure
 */
static inline int del_mdirents_file(int dirfd, mdirents_file_t *mdirents_file_p) {
    int status = -1;
    char pathname[ROZOFS_FILENAME_MAX];

    if (mdirents_file_p->header.mdirents_type == MDIRENTS_FILE_ROOT_TYPE) {
        build_path_lvl3_mdirents_file_root(pathname, mdirents_file_p->header.fid_cur.link_ref.hash_name_idx);
    } else {
        build_path_lvl3_mdirents_file_coll(pathname, mdirents_file_p->header.fid_cur.link_ref.fid);
    }

    if (unlinkat(dirfd, pathname, 0) == -1) {
        severe("unlinkat failed for file %s: %s", pathname, strerror(errno));
        goto out;
    }

    status = 0;
out:
    return status;
}

/**
 * API for delete mdirents file (ROOT type) when it's empty
 * 
 * @param dirfd: file descriptor of the parent directory
 * @param *mdirents_file_p: pointer to the mdirents file (ROOT type)
 * 
 * @retval  0 on success
 * @retval -1 on failure
 */
static inline int del_mdirents_file_root(int dirfd, mdirents_file_t * mdirents_file_p) {

    if (mdirents_file_p->header.mdirents_type != MDIRENTS_FILE_ROOT_TYPE)
        return -1;

    // If the mdirents file root is the only mdirents file for this hash value
    if (mdirents_file_p->header.fid_prev.link_type == MDIRENTS_FILE_ROOT_TYPE) {
        // Delete mdirents_file (disk)
        if (del_mdirents_file(dirfd, mdirents_file_p) == -1) {
            return -1;
        }
    } else {

        // The mdirents file root is NOT the only mdirents file for this hash value
        mdirents_file_t *next_mdirents_file_p = NULL;

        // Get the next mdirents file
        if ((next_mdirents_file_p = get_next_mdirents_file(dirfd, mdirents_file_p)) == NULL) {
            return -1;
        }

        // Delete the next mdirents file on disk only
        if (del_mdirents_file(dirfd, next_mdirents_file_p) == -1) {
            return -1;
        }

        // Change the next mdirents file colision into mdirents file root
        next_mdirents_file_p->header.mdirents_type = MDIRENTS_FILE_ROOT_TYPE;
        next_mdirents_file_p->header.fid_cur.link_type = MDIRENTS_FILE_ROOT_TYPE;
        next_mdirents_file_p->header.fid_cur.link_ref.hash_name_idx = mdirents_file_p->header.fid_cur.link_ref.hash_name_idx;

        // 2 cases:
        // - the next mdirents file is the only one mdirents file collision for
        // this hash value or not
        if (next_mdirents_file_p->header.fid_next.link_type == MDIRENTS_FILE_ROOT_TYPE) {
            next_mdirents_file_p->header.fid_prev.link_type = MDIRENTS_FILE_ROOT_TYPE;
            next_mdirents_file_p->header.fid_prev.link_ref.hash_name_idx = mdirents_file_p->header.fid_cur.link_ref.hash_name_idx;
        } else {
            next_mdirents_file_p->header.fid_prev.link_type = MDIRENTS_FILE_COLL_TYPE;
            memcpy(next_mdirents_file_p->header.fid_prev.link_ref.fid, mdirents_file_p->header.fid_prev.link_ref.fid, sizeof (fid_t));

            mdirents_file_t *next_next_mdirents_file_p = NULL;
            // Get the next next mdirents file
            if ((next_next_mdirents_file_p = get_next_mdirents_file(dirfd, next_mdirents_file_p)) == NULL) {
                return -1;
            }

            // Update the previous link of the next next mdirentry
            next_next_mdirents_file_p->header.fid_prev.link_type = MDIRENTS_FILE_ROOT_TYPE;
            next_next_mdirents_file_p->header.fid_prev.link_ref.hash_name_idx = mdirents_file_p->header.fid_cur.link_ref.hash_name_idx;

            // Write on disk the next next mdirents file root
            if (write_mdirents_file(dirfd, next_next_mdirents_file_p) == -1) {
                return -1;
            }

            free(next_next_mdirents_file_p);
        }

        // Write on disk the new mdirents file root
        if (write_mdirents_file(dirfd, next_mdirents_file_p) == -1) {
            return -1;
        }

        free(next_mdirents_file_p);
    }
    return 0;
}

/**
 * API for delete mdirents file (COLL type) when it's empty
 * 
 * @param dirfd: file descriptor of the parent directory
 * @param *mdirents_file_p: pointer to the mdirents file (COLL type)
 * 
 * @retval  0 on success
 * @retval -1 on failure
 */
static inline int del_mdirents_file_coll(int dirfd, mdirents_file_t * mdirents_file_p) {

    if (mdirents_file_p->header.mdirents_type != MDIRENTS_FILE_COLL_TYPE) {
        return -1;
    }

    // If the mdirents file collision to delete is the only one for this hash value
    if (mdirents_file_p->header.fid_prev.link_type == MDIRENTS_FILE_ROOT_TYPE && mdirents_file_p->header.fid_next.link_type == MDIRENTS_FILE_ROOT_TYPE) {
        char pathname_root[ROZOFS_FILENAME_MAX];
        mdirents_file_t * root_mdirents_file_p = NULL;

        // Build the pathname
        build_path_lvl3_mdirents_file_root(pathname_root, mdirents_file_p->header.fid_prev.link_ref.hash_name_idx);

        // Read on disk 
        if ((root_mdirents_file_p = read_mdirents_file(dirfd, pathname_root)) == NULL) {
            return -1;
        }

        // Change the next and previous links
        root_mdirents_file_p->header.fid_prev = root_mdirents_file_p->header.fid_cur;
        root_mdirents_file_p->header.fid_next = root_mdirents_file_p->header.fid_cur;

        // Write on disk
        if (write_mdirents_file(dirfd, root_mdirents_file_p) == -1) {
            return -1;
        }

        free(root_mdirents_file_p);

    } else {
        char pathname_prev[ROZOFS_FILENAME_MAX];
        char pathname_next[ROZOFS_FILENAME_MAX];
        mdirents_file_t * next_mdirents_file_p = NULL;
        mdirents_file_t * prev_mdirents_file_p = NULL;

        // Get the previous mdirents_file
        if (mdirents_file_p->header.fid_prev.link_type == MDIRENTS_FILE_COLL_TYPE) {
            //Build the pathname
            build_path_lvl3_mdirents_file_coll(pathname_prev, mdirents_file_p->header.fid_prev.link_ref.fid);

        } else {
            //Build the pathname
            build_path_lvl3_mdirents_file_root(pathname_prev, mdirents_file_p->header.fid_prev.link_ref.hash_name_idx);
        }

        if ((prev_mdirents_file_p = read_mdirents_file(dirfd, pathname_prev)) == NULL) {
            return -1;
        }

        // Get the next mdirents_file
        if (mdirents_file_p->header.fid_next.link_type == MDIRENTS_FILE_COLL_TYPE) {
            //Build the pathname
            build_path_lvl3_mdirents_file_coll(pathname_next, mdirents_file_p->header.fid_next.link_ref.fid);

        } else {
            //Build the pathname
            build_path_lvl3_mdirents_file_root(pathname_next, mdirents_file_p->header.fid_next.link_ref.hash_name_idx);
        }

        if ((next_mdirents_file_p = read_mdirents_file(dirfd, pathname_next)) == NULL) {
            return -1;
        }
        // Update the next and previous mdirents_files (memory)
        next_mdirents_file_p->header.fid_prev = prev_mdirents_file_p->header.fid_cur;
        prev_mdirents_file_p->header.fid_next = next_mdirents_file_p->header.fid_cur;

        // Update the next and previous mdirents_files (disk)
        if (write_mdirents_file(dirfd, prev_mdirents_file_p) == -1) {
            return -1;
        }
        if (write_mdirents_file(dirfd, next_mdirents_file_p) == -1) {
            return -1;
        }

        free(prev_mdirents_file_p);
        free(next_mdirents_file_p);
    }

    // Delete mdirents_file (disk)
    if (del_mdirents_file(dirfd, mdirents_file_p) == -1) {
        return -1;
    }

    return 0;
}

/**
 *  API for searching an index of free mdirentry in one mdirents file
 * 
 * @param *mdirents_file_p: pointer to the mdirents file (memory)
 * @retval index of the free mdirentry or -1 if none
 */
static inline int get_free_mdirentry_in_mdirents_file(mdirents_file_t *mdirents_file_p) {
    int i = 0;

    // Full
    if (mdirents_file_p->header.bitmap == FULL_MDIRENTS_BITMAP) {
        return -1;
    }

    for (i = 0; i < MDIRENTS_FILE_MAX_ENTRY; i++) {

        if ((mdirents_file_p->header.bitmap & (0x1 << i)) == 0) {
            // One free entry found
            return i;
        }
    }
    // That should not occur !
    return -1;
}

/**
 *  API to insert an mdirentry in a mdirents file.
 *  The caller must allocate a free idx first.
 *
 * @param dirfd: file descriptor of the parent directory
 * @param *mdirents_file_p: pointer to the mdirents file (memory)
 * @param entry_idx: index where to insert the mdirentry ( get from get_free_mdirentry_in_mdirents_file())
 * @param type: type of the mdirentry to put
 * @param fid: unique identifier of the mdirentry to put
 * @param *name: pointer to the name of the mdirentry to put
 * 
 * @retval  0 on success
 * @retval -1 on failure : the index of the mdirentry is out of range
 */
static inline int put_mdirentry_in_mdirents_file(int dirfd, mdirents_file_t *mdirents_file_p, int entry_idx, uint32_t type, fid_t fid, char *name) {

    mdirents_entry_t *entry_p = NULL;

    if ((entry_idx < 0) || (entry_idx > (MDIRENTS_FILE_MAX_ENTRY - 1))) {
        // Index is out of range 
        return -1;
    }

    entry_p = &(mdirents_file_p->mdirentry[entry_idx]);
    memcpy(entry_p->fid, fid, sizeof (fid_t));

    // Caution : here it is assumed that the length of the name has already been checked!!
    strcpy(entry_p->name, name);
    entry_p->type = type;
    // OK now, assert the corresponding bit in the bitmap
    mdirents_file_p->header.bitmap |= (1 << entry_idx);

    // Write on disk
    if (write_mdirents_file(dirfd, mdirents_file_p) == -1) {
        return -1;
    }

    return 0;
}

/**
 *  API to delete an mdirentry in one mdirents file.
 *  The caller must know the idx of mdirentry to delete first.
 *
 * @param dirfd: file descriptor of the parent directory
 * @param mdirents_file_p: pointer to the mdirents file (memory)
 * @param entry_idx: index where the mdirentry to delete is,
 * (get from search_mdirentry_by_name_in_mdirents())
 * 
 * @retval  0 on success
 * @retval -1 on failure : the index of the mdirentry is out of range
 */
static inline int del_mdirentry_in_mdirents_file(int dirfd, mdirents_file_t *mdirents_file_p, int entry_idx) {

    mdirents_entry_t *entry_p = NULL;

    if ((entry_idx < 0) || (entry_idx > (MDIRENTS_FILE_MAX_ENTRY - 1))) {
        // Index is out of range
        return -1;
    }

    entry_p = &(mdirents_file_p->mdirentry[entry_idx]);

    // OK now, assert the corresponding bit in the bitmap
    mdirents_file_p->header.bitmap &= ~(1 << entry_idx);

    // If it's the last mdirentry in this file
    if (mdirents_file_p->header.bitmap == 0) {
        // Now it's a empty file

        // 2 cases : it's a root or a collision mdirents file
        if (mdirents_file_p->header.mdirents_type == MDIRENTS_FILE_ROOT_TYPE) {

            if (del_mdirents_file_root(dirfd, mdirents_file_p) == -1) {
                return -1;
            }

        } else {

            if (del_mdirents_file_coll(dirfd, mdirents_file_p) == -1) {
                return -1;
            }
        }

    } else {
        // It's not the last mdirentry in this file
        // Write on disk
        if (write_mdirents_file(dirfd, mdirents_file_p) == -1) {
            return -1;
        }
    }
    return 0;
}

/**
 *  API for searching an mdirentry within one mdirents file with a key
 *  that is the name.
 *
 * @param *mdirents_file_p: pointer to the dirent file
 * @param *name: (key) pointer to the name of the mdirentry to search
 * 
 * @retval index of the mdirentry within the mdirents file
 * @retval -1 when mdirentry is not found
 */
static inline int search_mdirentry_by_name_in_mdirents_file(mdirents_file_t *mdirents_file_p, char *name) {

    mdirents_entry_t *entry_p = NULL;
    int i = 0;
    // Check if the mdirents file is empty
    if (mdirents_file_p->header.bitmap == 0) {
        // Empty file
        return -1;
    }
    // Search among the non-empty mdirentries
    for (i = 0; i < MDIRENTS_FILE_MAX_ENTRY; i++) {

        // If is empty
        if ((mdirents_file_p->header.bitmap & (0x1 << i)) == 0) {
            continue;
        }
        entry_p = &mdirents_file_p->mdirentry[i];

        // Check name
        if (strcmp(entry_p->name, name) != 0) continue;

        // Mdirentry found!

        return i;
    }

    return -1;
}

/**
 *  API for searching an mdirentry within one mdirents file with 
 *  a key that is the fid of the mdirentry
 *
 * @param *mdirents_file_p: pointer to the dirent file
 * @param fid: (key) pointer to the name of the mdirentry to search
 * 
 * @retval index of the mdirentry within the mdirents file
 * @retval -1 when not found
 */
static inline int search_mdirentry_by_fid_in_mdirents_file(mdirents_file_t *mdirents_file_p, fid_t fid) {

    mdirents_entry_t *entry_p;
    int i;
    // Check if the dirent file is empty
    if (mdirents_file_p->header.bitmap == 0) {
        // Empty file
        return -1;
    }
    // Search among the non-empty entries
    for (i = 0; i < MDIRENTS_FILE_MAX_ENTRY; i++) {

        if ((mdirents_file_p->header.bitmap & (0x1 << i)) == 0) continue;

        entry_p = &mdirents_file_p->mdirentry[i];

        if (memcmp(entry_p->fid, fid, sizeof (fid_t)) != 0) continue;
        // Entry found!

        return i;
    }
    return -1;
}

/**
 * API that search the presence of mdirentry with key=name in a parent directory
 * 
 * @param dirfd: file descriptor of the parent directory
 * @param name: (key) pointer to the name to search
 * @param idx_ptr: pointer to the relative idx within the directory entry
 * 
 * @retval NULL if not found
 * @retval pointer to the mdirents file and *idx_ptr contains the index
 * of this mdirentry
 */

static inline mdirents_file_t * search_mdirentry_by_name_in_mdirents(int dirfd, char *name, int *idx_ptr) {
    char path[PATH_MAX];
    int entry_idx = -1;
    mdirents_file_t *current_mdirents_file_p = NULL;
    mdirents_file_t *next_mdirents_file_p = NULL;

    // Build the absolute pathname of the level 1 dirent
    build_path_lvl3_mdirents_file_root(path, hash_name(name));

    // Read the root file: either read it directory on disk or get it from a cache
    if (!(current_mdirents_file_p = read_mdirents_file(dirfd, path))) {
        // Not found
        errno = ENOENT;
        return NULL;
    }

    while (current_mdirents_file_p != NULL) {
        // Search within the mdirents file if the name and type exist
        entry_idx = search_mdirentry_by_name_in_mdirents_file(current_mdirents_file_p, name);
        if (entry_idx != -1) {
            // mdirentry has been found
            *idx_ptr = entry_idx;

            return current_mdirents_file_p;
        }
        // Not found in the current mdirents file try the next one if any
        next_mdirents_file_p = get_next_mdirents_file(dirfd, current_mdirents_file_p);
        free(current_mdirents_file_p);
        current_mdirents_file_p = next_mdirents_file_p;

    }
    // Not found
    errno = ENOENT;

    return NULL;
}

/**
 * API for searching a free mdirentry of a file or directory in one parent
 * directory
 * 
 * @param dirfd: file descriptor of the parent directory
 * @param hash_value: result value from the hashing of the name of mdirentry
 * @param *idx_ptr: pointer to the relative idx within the directory entry
 * 
 * @retval NULL if not found
 * @retval pointer to the current mdirents file and *idx_ptr contains the free
 *  index to use
 */
static inline mdirents_file_t * search_free_mdirentry_in_mdirents(int dirfd, uint32_t hash_value, int *idx_ptr) {
    char path[PATH_MAX];
    int entry_idx = -1;
    mdirents_file_t *current_mdirents_file_p = NULL;
    mdirents_file_t *next_mdirents_file_p = NULL;
    mdirents_file_t *mdirents_file_root_p = NULL;

    // Build the absolute pathname of the level 1 dirent
    build_path_lvl3_mdirents_file_root(path, hash_value);

    // Read the root mdirents file: either read it directory on disk or get it from a cache 

    // Get mdirents_file_root from disk
    if (!(mdirents_file_root_p = read_mdirents_file(dirfd, path))) {
        // If the mdirents_file (ROOT) not exist on disk
        // Create the mdirents_file_t structure
        mdirents_file_root_p = xmalloc(sizeof (mdirents_file_t));
        memset(mdirents_file_root_p, 0, sizeof (mdirents_file_t));
        mdirents_file_root_p->header.mdirents_type = MDIRENTS_FILE_ROOT_TYPE;
        mdirents_file_root_p->header.bitmap = 0;
        mdirents_file_root_p->header.fid_cur.link_type = MDIRENTS_FILE_ROOT_TYPE;
        mdirents_file_root_p->header.fid_cur.link_ref.hash_name_idx = hash_value;
        mdirents_file_root_p->header.fid_next.link_type = MDIRENTS_FILE_ROOT_TYPE;
        mdirents_file_root_p->header.fid_next.link_ref.hash_name_idx = hash_value;
        mdirents_file_root_p->header.fid_prev.link_type = MDIRENTS_FILE_ROOT_TYPE;
        mdirents_file_root_p->header.fid_prev.link_ref.hash_name_idx = hash_value;
    }

    current_mdirents_file_p = mdirents_file_root_p;

    while (current_mdirents_file_p != NULL) {

        entry_idx = get_free_mdirentry_in_mdirents_file(current_mdirents_file_p);
        if (entry_idx != -1) {
            // One free mdirentry has been found
            *idx_ptr = entry_idx;
            if (current_mdirents_file_p->header.mdirents_type == MDIRENTS_FILE_COLL_TYPE)
                free(mdirents_file_root_p);
            return current_mdirents_file_p;
        }
        // Not found in the current dirent file try the next one if any
        if ((next_mdirents_file_p = get_next_mdirents_file(dirfd, current_mdirents_file_p)) == NULL) {
            // If the next mdirents file (collision) doesn't exist on disk
            // Create the next_mdirents_file_p
            next_mdirents_file_p = xmalloc(sizeof (mdirents_file_t));
            memset(next_mdirents_file_p, 0, sizeof (mdirents_file_t));
            next_mdirents_file_p->header.mdirents_type = MDIRENTS_FILE_COLL_TYPE;
            next_mdirents_file_p->header.bitmap = 0;

            next_mdirents_file_p->header.fid_cur.link_type = MDIRENTS_FILE_COLL_TYPE;
            uuid_generate(next_mdirents_file_p->header.fid_cur.link_ref.fid);

            next_mdirents_file_p->header.fid_next = mdirents_file_root_p->header.fid_cur;
            next_mdirents_file_p->header.fid_prev = current_mdirents_file_p->header.fid_cur;

            mdirents_file_root_p->header.fid_prev = next_mdirents_file_p->header.fid_cur;
            current_mdirents_file_p->header.fid_next = next_mdirents_file_p->header.fid_cur;

            // Write on disk
            if (current_mdirents_file_p->header.mdirents_type == MDIRENTS_FILE_ROOT_TYPE) {
                if (write_mdirents_file(dirfd, mdirents_file_root_p) == -1) {
                    return NULL;
                }
            } else {
                if (write_mdirents_file(dirfd, mdirents_file_root_p) == -1) {
                    return NULL;
                }
                if (write_mdirents_file(dirfd, current_mdirents_file_p) == -1) {
                    return NULL;
                }
            }
        }

        if (current_mdirents_file_p->header.mdirents_type == MDIRENTS_FILE_COLL_TYPE)
            free(current_mdirents_file_p);
        current_mdirents_file_p = next_mdirents_file_p;
    }
    // That should not occur !
    return NULL;
}

int put_mdirentry(mdir_t * mdir, char * name, fid_t fid, uint32_t type) {
    int status = -1;
    mdirents_file_t * mdirents_file_p = NULL;
    int idx_ptr = -1;

    int dirfd = mdir->fdp;

    if ((mdirents_file_p = search_mdirentry_by_name_in_mdirents(dirfd, name, &idx_ptr)) != NULL) {
        // This mdirentry exists

        // Replace mdirentry
        if (put_mdirentry_in_mdirents_file(dirfd, mdirents_file_p, idx_ptr, type, fid, name) == -1) {
            goto out;
        }

    } else {
        // This mdirentry not exists

        // Search free mdirentry
        if ((mdirents_file_p = search_free_mdirentry_in_mdirents(dirfd, hash_name(name), &idx_ptr)) == NULL) {
            goto out;
        }

        // Insert the new mdirenrty
        if (put_mdirentry_in_mdirents_file(dirfd, mdirents_file_p, idx_ptr, type, fid, name) == -1) {
            goto out;
        }

    }

    status = 0;
out:
    if (mdirents_file_p)
        free(mdirents_file_p);

    return status;
}

int get_mdirentry(mdir_t * mdir, char * name, fid_t fid, uint32_t * type) {
    int status = -1;
    mdirents_file_t * mdirents_file_p = NULL;
    int idx_ptr = -1;

    int dirfd = mdir->fdp;

    if ((mdirents_file_p = search_mdirentry_by_name_in_mdirents(dirfd, name, &idx_ptr)) == NULL)
        goto out;

    memcpy(fid, mdirents_file_p->mdirentry[idx_ptr].fid, sizeof (fid_t));
    memcpy(type, &mdirents_file_p->mdirentry[idx_ptr].type, sizeof (uint32_t));

    status = 0;
out:
    if (mdirents_file_p)
        free(mdirents_file_p);

    return status;
}

int del_mdirentry(mdir_t * mdir, char * name, fid_t fid, uint32_t * type) {
    int status = -1;
    mdirents_file_t * mdirents_file_p = NULL;
    int idx_ptr = -1;

    int dirfd = mdir->fdp;

    if ((mdirents_file_p = search_mdirentry_by_name_in_mdirents(dirfd, name, &idx_ptr)) == NULL) {
        goto out;
    }

    memcpy(fid, mdirents_file_p->mdirentry[idx_ptr].fid, sizeof (fid_t));
    memcpy(type, &mdirents_file_p->mdirentry[idx_ptr].type, sizeof (uint32_t));

    if (del_mdirentry_in_mdirents_file(dirfd, mdirents_file_p, idx_ptr) == -1) {
        goto out;
    }

    status = 0;
out:
    if (mdirents_file_p)
        free(mdirents_file_p);
    return status;
}

int list_mdirentries(mdir_t * mdir, child_t ** children, uint64_t cookie, uint8_t * eof) {
    DIR *dp = NULL;
    struct dirent *ep = NULL;
    mdirents_file_t * mdirents_file_p = NULL;
    child_t ** iterator;
    mdirents_entry_t *entry_p = NULL;
    int i = 0;
    int j = 0;
    int c = 0;

    int fd = mdir->fdp;

    if ((dp = fdopendir(dup(fd))) == NULL) {
        severe("fdopendir failed %s", strerror(errno));
        return -1;
    }

    iterator = children;

    // For each file
    while (((ep = readdir(dp)) != NULL && j < MAX_DIR_ENTRIES)) {

        // manage . .. and file for mattrs for this directory

        if (strcmp(ep->d_name, ".") == 0)
            continue;

        if (strcmp(ep->d_name, "..") == 0)
            continue;

        if (strcmp(ep->d_name, MDIR_ATTRS_FNAME) == 0)
            continue;

        // Read mdirents file
        if ((mdirents_file_p = read_mdirents_file(fd, ep->d_name)) == NULL) {
            return -1; // That not shoud occurs
        }

        // Search among the non-empty mdirentries
        for (i = 0; i < MDIRENTS_FILE_MAX_ENTRY; i++) {

            // If is this mdirentry is empty
            if ((mdirents_file_p->header.bitmap & (0x1 << i)) == 0) continue;

            // Entry found
            entry_p = &mdirents_file_p->mdirentry[i];

            if (c >= cookie) {
                *iterator = xmalloc(sizeof (child_t));
                memset(*iterator, 0, sizeof (child_t));
                memcpy((*iterator)->fid, entry_p->fid, sizeof (fid_t));
                (*iterator)->name = xstrdup(entry_p->name);
                // Go to next entry
                iterator = &(*iterator)->next;
                j++;
            }

            c++;
        }
        free(mdirents_file_p);
    }

    if (ep)
        *eof = 0;
    else
        *eof = 1;

    rewinddir(dp);

    if (closedir(dp) == -1)
        return -1;

    return 0;
}
