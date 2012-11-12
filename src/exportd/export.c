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

#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <attr/xattr.h>
#include <sys/vfs.h>
#include <uuid/uuid.h>
#include <sys/types.h>
#include <inttypes.h>
#include <dirent.h>
#include <time.h>

#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/list.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/epproto.h>
#include <rozofs/rpc/mclient.h>

#include "config.h"
#include "export.h"
#include "cache.h"
#include "mdirent_vers2.h"

/** max entries of lv1 directory structure */
#define MAX_LV1_BUCKETS 256
#define LV1_NOCREATE 0
#define LV1_CREATE 1
#define RM_FILES_MAX 2500

typedef struct rmfentry {
    fid_t fid;
    sid_t sids[ROZOFS_SAFE_MAX];
    list_t list;
} rmfentry_t;

typedef struct cnxentry {
    mclient_t *cnx;
    list_t list;
} cnxentry_t;

DECLARE_PROFILING(epp_profiler_t);

/** get the lv1 directory.
 *
 * lv1 entries are first level directories of an export root named by uint32_t
 * string value and used has entry of a hash table storing the export
 * meta data files.
 *
 * @param export: the export used to find directory
 * @param fid: the search fid
 * @param create: whenever directory will be created or not
 *
 * @return the entry on success otherwise -1
 */
static int export_lv1_resolve_entry(export_t *export, fid_t fid, int create) {
    uint32_t hash = 0;
    uint8_t *c = 0;
    char path[PATH_MAX];
    START_PROFILING(export_lv1_resolve_entry);

    for (c = fid; c != fid + 16; c++)
        hash = *c + (hash << 6) + (hash << 16) - hash;
    hash %= MAX_LV1_BUCKETS;
    sprintf(path, "%s/%"PRId32"", export->root, hash);
    if (access(path, F_OK) == -1) {
        if (errno == ENOENT && create) {
            if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) != 0)
                return -1;
        } else {
            return -1;
        }
    }

    STOP_PROFILING(export_lv1_resolve_entry);
    return hash;
}

/** build a full path based on export root and fid of the lv2 file
 *
 * lv2 is the second level of files or directories in storage of metadata
 * they are acceded thru mreg or mdir API according to their type.
 *
 * @param export: the export we are searching on
 * @param fid: the fid we are looking for
 * @param path: the path to fill in
 */
static int export_lv2_resolve_path(export_t *export, fid_t fid, char *path) {
    int lv1 = -1;
    char str[37];
    START_PROFILING(export_lv2_resolve_path);

    if ((lv1 = export_lv1_resolve_entry(export, fid, LV1_CREATE)) < 0)
        return -1;

    uuid_unparse(fid, str);
    sprintf(path, "%s/%d/%s", export->root, lv1, str);

    STOP_PROFILING(export_lv2_resolve_path);
    return 0;
}

/** search a fid in the cache
 *
 * if fid is not cached, try to find it on the underlying file system
 * and cache it.
 *
 * @param e: the underlying export
 * @param fid: the searched fid
 *
 * @return a pointer to lv2 entry or null on error (errno is set)
 */
static lv2_entry_t *export_lookup_fid(export_t *e, fid_t fid) {
    lv2_entry_t *lv2 = 0;
    START_PROFILING(export_lookup_fid);

    if (!(lv2 = lv2_cache_get(e->lv2_cache, fid))) {
        // not cached, find it an cache it
        char lv2_path[PATH_MAX];
        if (export_lv2_resolve_path(e, fid, lv2_path) != 0) {
            return lv2;
        }
        if (!(lv2 = lv2_cache_put(e->lv2_cache, fid, lv2_path))) {
            return lv2;
        }
    }

    STOP_PROFILING(export_lookup_fid);
    return lv2;
}

/** store the attributes part of a lv2_entry_t to the export's file system
 *
 * @param entry: the entry used
 *
 * @return: 0 on success otherwise -1
 */
static int export_lv2_write_attributes(lv2_entry_t *entry) {

    if (S_ISDIR(entry->attributes.mode)) {
        return mdir_write_attributes(&entry->container.mdir, &entry->attributes);
    } else if (S_ISREG(entry->attributes.mode)) {
        return mreg_write_attributes(&entry->container.mreg, &entry->attributes);
    } else if (S_ISLNK(entry->attributes.mode)) {
        return mslnk_write_attributes(&entry->container.mslnk, &entry->attributes);
    } else {
        errno = ENOTSUP;
        return -1;
    }
}

/** set an extended attribute value for a lv2_entry_t.
 *
 * @param entry: the entry used
 * @param name: the extended attribute name.
 * @param value: the value of this extended attribute.
 * @param size: the size of a buffer to hold the value associated
 *  with this extended attribute.
 * @param flags: parameter can be used to refine the semantics of the operation.
 * 
 * @return: On success, zero is returned.  On failure, -1 is returned.
 */
static int export_lv2_set_xattr(lv2_entry_t *entry, const char *name, const void *value, size_t size, int flags) {

    if (S_ISDIR(entry->attributes.mode)) {
        return mdir_set_xattr(&entry->container.mdir, name, value, size, flags);
    } else if (S_ISREG(entry->attributes.mode)) {
        return mreg_set_xattr(&entry->container.mreg, name, value, size, flags);
    } else if (S_ISLNK(entry->attributes.mode)) {
        return mslnk_set_xattr(&entry->container.mslnk, name, value, size, flags);
    } else {
        errno = ENOTSUP;
        return -1;
    }
}

/** retrieve an extended attribute value from the lv2_entry_t.
 *
 * @param entry: the entry used
 * @param name: the extended attribute name.
 * @param value: the value of this extended attribute.
 * @param size: the size of a buffer to hold the value associated
 *  with this extended attribute.
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
static ssize_t export_lv2_get_xattr(lv2_entry_t *entry, const char *name, void *value, size_t size) {

    if (S_ISDIR(entry->attributes.mode)) {
        return mdir_get_xattr(&entry->container.mdir, name, value, size);
    } else if (S_ISREG(entry->attributes.mode)) {
        return mreg_get_xattr(&entry->container.mreg, name, value, size);
    } else if (S_ISLNK(entry->attributes.mode)) {
        return mslnk_get_xattr(&entry->container.mslnk, name, value, size);
    } else {
        errno = ENOTSUP;
        return -1;
    }
}

/** remove an extended attribute from the lv2_entry_t.
 *
 * @param entry: the entry used
 * @param name: the extended attribute name.
 * 
 * @return: On success, zero is returned.  On failure, -1 is returned.
 */
static int export_lv2_remove_xattr(lv2_entry_t *entry, const char *name) {

    if (S_ISDIR(entry->attributes.mode)) {
        return mdir_remove_xattr(&entry->container.mdir, name);
    } else if (S_ISREG(entry->attributes.mode)) {
        return mreg_remove_xattr(&entry->container.mreg, name);
    } else if (S_ISLNK(entry->attributes.mode)) {
        return mslnk_remove_xattr(&entry->container.mslnk, name);
    } else {
        errno = ENOTSUP;
        return -1;
    }
}

/** list extended attribute names from the lv2_entry_t.
 *
 * @param entry: the entry used
 * @param list: list of extended attribute names associated with this directory.
 * @param size: the size of a buffer to hold the list of extended attributes.
 * 
 * @return: On success, the size of the extended attribute name list.
 * On failure, -1 is returned and errno is set appropriately.
 */
static ssize_t export_lv2_list_xattr(lv2_entry_t *entry, void *list, size_t size) {

    if (S_ISDIR(entry->attributes.mode)) {
        return mdir_list_xattr(&entry->container.mdir, list, size);
    } else if (S_ISREG(entry->attributes.mode)) {
        return mreg_list_xattr(&entry->container.mreg, list, size);
    } else if (S_ISLNK(entry->attributes.mode)) {
        return mslnk_list_xattr(&entry->container.mslnk, list, size);
    } else {
        errno = ENOTSUP;
        return -1;
    }
}

/** update the number of files in file system
 *
 * @param e: the export to update
 * @param n: number of files
 *
 * @return 0 on success -1 otherwise
 */
static int export_update_files(export_t *e, int32_t n) {
    int status = -1;
    START_PROFILING(export_update_files);

    e->fstat.files += n;
    if (pwrite(e->fdstat, &e->fstat, sizeof (export_fstat_t), 0)
            != sizeof (export_fstat_t))
        goto out;

    status = 0;
out:
    STOP_PROFILING(export_update_files);
    return status;
}

/** update the number of blocks in file system
 *
 * @param e: the export to update
 * @param n: number of blocks
 *
 * @return 0 on success -1 otherwise
 */
static int export_update_blocks(export_t * e, int32_t n) {
    int status = -1;
    START_PROFILING(export_update_blocks);

    if (e->hquota > 0 && e->fstat.blocks + n > e->hquota) {
        warning("quota exceed: %"PRIu64" over %"PRIu64"", e->fstat.blocks + n,
                e->hquota);
        errno = EDQUOT;
        goto out;
    }

    e->fstat.blocks += n;
    if (pwrite(e->fdstat, &e->fstat, sizeof (export_fstat_t), 0)
            != sizeof (export_fstat_t))
        goto out;

    status = 0;
out:
    STOP_PROFILING(export_update_blocks);
    return status;
}

/** constants of the export */
typedef struct export_const {
    char version[20]; ///< rozofs version
    fid_t rfid; ///< root id
} export_const_t;

int export_is_valid(const char *root) {
    char path[PATH_MAX];
    char trash_path[PATH_MAX];
    char fstat_path[PATH_MAX];
    char const_path[PATH_MAX];

    if (!realpath(root, path))
        return -1;

    if (access(path, R_OK | W_OK | X_OK) != 0)
        return -1;

    // check trash directory
    sprintf(trash_path, "%s/%s", path, TRASH_DNAME);
    if (access(trash_path, F_OK) != 0)
        return -1;

    // check fstat file
    sprintf(fstat_path, "%s/%s", path, FSTAT_FNAME);
    if (access(fstat_path, F_OK) != 0)
        return -1;

    // check const file
    sprintf(const_path, "%s/%s", path, CONST_FNAME);
    if (access(const_path, F_OK) != 0)
        return -1;

    return 0;
}

static int export_load_rmfentry(export_t * e) {
    int status = -1;
    DIR *dd = NULL;
    struct dirent *dp;
    rmfentry_t *rmfe = NULL;
    char trash_path[PATH_MAX];

    sprintf(trash_path, "%s/%s", e->root, TRASH_DNAME);

    if ((dd = opendir(trash_path)) == NULL) {
        severe("opendir (trash directory) failed: %s", strerror(errno));
        goto out;
    }

    while ((dp = readdir(dd)) != NULL) {

        if ((strcmp(dp->d_name, ".") == 0) || (strcmp(dp->d_name, "..") == 0)) {
            continue;
        }

        char rm_path[PATH_MAX];
        int fd = -1;
        mattr_t attrs;

        sprintf(rm_path, "%s/%s/%s", e->root, TRASH_DNAME, dp->d_name);

        // Open file to delete
        if ((fd = open(rm_path, O_RDWR, S_IRWXU)) == -1) {
            severe("open (file under trash directory) failed: %s", strerror(errno));
            goto out;
        }

        // Read file to delete
        if ((pread(fd, &attrs, sizeof (mattr_t), 0)) != (sizeof (mattr_t))) {
            severe("pread (file under trash directory) failed: %s", strerror(errno));
            goto out;
        }

        rmfe = xmalloc(sizeof (rmfentry_t));
        memcpy(rmfe->fid, attrs.fid, sizeof (fid_t));
        memcpy(rmfe->sids, attrs.sids, sizeof (sid_t) * ROZOFS_SAFE_MAX);

        list_init(&rmfe->list);

        if ((errno = pthread_rwlock_wrlock(&e->rm_lock)) != 0)
            goto out;

        list_push_front(&e->rmfiles, &rmfe->list);

        if ((errno = pthread_rwlock_unlock(&e->rm_lock)) != 0)
            goto out;

        if (fd != -1) {
            close(fd);
        }

    }
    status = 0;
out:
    if (dd != NULL) {
        closedir(dd);
    }
    return status;
}

int export_create(const char *root) {
    const char *version = VERSION;
    char path[PATH_MAX];
    char trash_path[PATH_MAX];
    char fstat_path[PATH_MAX];
    char const_path[PATH_MAX];
    char root_path[PATH_MAX];
    export_fstat_t est;
    export_const_t ect;
    int fd = -1;
    uint32_t lv1 = 0;
    uint8_t *c = 0;
    char fidstr[37];
    mattr_t root_attrs;
    mdir_t root_mdir;

    if (!realpath(root, path))
        return -1;

    // create trash directory
    sprintf(trash_path, "%s/%s", path, TRASH_DNAME);
    if (mkdir(trash_path, S_IRUSR | S_IWUSR | S_IXUSR) < 0) {
        return -1;
    }

    // create fstat file
    sprintf(fstat_path, "%s/%s", path, FSTAT_FNAME);
    if ((fd = open(fstat_path, O_RDWR | O_CREAT, S_IRWXU)) < 1) {
        return -1;
    }

    est.blocks = est.files = 0;
    if (write(fd, &est, sizeof (export_fstat_t)) != sizeof (export_fstat_t)) {
        close(fd);
        return -1;
    }
    close(fd);

    //create root
    memset(&root_attrs, 0, sizeof (mattr_t));
    uuid_generate(root_attrs.fid);
    root_attrs.cid = 0;
    memset(root_attrs.sids, 0, ROZOFS_SAFE_MAX * sizeof (sid_t));
    root_attrs.mode = S_IFDIR | S_IRUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH
            | S_IWOTH | S_IXOTH;
    root_attrs.nlink = 2;
    root_attrs.uid = 0; // root
    root_attrs.gid = 0; // root
    if ((root_attrs.ctime = root_attrs.atime = root_attrs.mtime = time(NULL)) == -1)
        return -1;
    root_attrs.size = ROZOFS_DIR_SIZE;

    // create the lv1 directory
    for (c = root_attrs.fid; c != root_attrs.fid + 16; c++)
        lv1 = *c + (lv1 << 6) + (lv1 << 16) - lv1;
    lv1 %= MAX_LV1_BUCKETS;
    sprintf(root_path, "%s/%u", path, lv1);
    if (mkdir(root_path, S_IRUSR | S_IWUSR | S_IXUSR) != 0)
        return -1;

    // create the lv2 directory
    uuid_unparse(root_attrs.fid, fidstr);
    sprintf(root_path, "%s/%u/%s", root, lv1, fidstr);
    if (mkdir(root_path, S_IRUSR | S_IWUSR | S_IXUSR) != 0)
        return -1;

    // open the root mdir
    if (mdir_open(&root_mdir, root_path) != 0) {
        return -1;
    }

    // set children count to 0
    root_mdir.children = 0;

    if (mdir_write_attributes(&root_mdir, &root_attrs) != 0) {
        mdir_close(&root_mdir);
        return -1;
    }

    // Initialize the dirent level 0 cache
    dirent_cache_level0_initialize();

    // create "." ".." lv3 entries
    if (put_mdirentry(root_mdir.fdp, root_attrs.fid, ".", root_attrs.fid, S_IFDIR | S_IRWXU) != 0) {
        mdir_close(&root_mdir);
        return -1;
    }
    if (put_mdirentry(root_mdir.fdp, root_attrs.fid, "..", root_attrs.fid, S_IFDIR | S_IRWXU) != 0) {
        mdir_close(&root_mdir);
        return -1;
    }
    mdir_close(&root_mdir);

    // create const file.
    memset(&ect, 0, sizeof (export_const_t));
    uuid_copy(ect.rfid, root_attrs.fid);
    strcpy(ect.version, version);
    sprintf(const_path, "%s/%s", path, CONST_FNAME);
    if ((fd = open(const_path, O_RDWR | O_CREAT, S_IRWXU)) < 1) {
        return -1;
    }

    if (write(fd, &ect, sizeof (export_const_t)) != sizeof (export_const_t)) {
        close(fd);
        return -1;
    }
    close(fd);

    return 0;
}

int export_initialize(export_t * e, volume_t *volume, lv2_cache_t *lv2_cache,
        uint32_t eid, const char *root, const char *md5, uint64_t squota,
        uint64_t hquota) {

    char fstat_path[PATH_MAX];
    char const_path[PATH_MAX];
    char root_path[PATH_MAX];
    export_const_t ect;
    int fd;

    if (!realpath(root, e->root))
        return -1;

    e->eid = eid;
    e->volume = volume;
    e->lv2_cache = lv2_cache;

    // Initialize the dirent level 0 cache
    dirent_cache_level0_initialize();

    if (strlen(md5) == 0) {
        memcpy(e->md5, ROZOFS_MD5_NONE, ROZOFS_MD5_SIZE);
    } else {
        memcpy(e->md5, md5, ROZOFS_MD5_SIZE);
    }
    e->squota = squota;
    e->hquota = hquota;

    // open the export_stat file an load it
    sprintf(fstat_path, "%s/%s", e->root, FSTAT_FNAME);
    if ((e->fdstat = open(fstat_path, O_RDWR)) < 0)
        return -1;
    if (pread(e->fdstat, &e->fstat, sizeof (export_fstat_t), 0)
            != sizeof (export_fstat_t))
        return -1;

    // Register the root
    sprintf(const_path, "%s/%s", e->root, CONST_FNAME);
    if ((fd = open(const_path, O_RDWR, S_IRWXU)) < 1) {
        return -1;
    }

    if (read(fd, &ect, sizeof (export_const_t)) != sizeof (export_const_t)) {
        close(fd);
        return -1;
    }
    close(fd);
    uuid_copy(e->rfid, ect.rfid);

    if (export_lv2_resolve_path(e, e->rfid, root_path) != 0) {
        close(e->fdstat);
        return -1;
    }

    if (!lv2_cache_put(e->lv2_cache, e->rfid, root_path)) {
        close(e->fdstat);
        return -1;
    }

    // Initialize list of files to delete
    list_init(&e->rmfiles);

    // Initialize lock for the list of files to delete
    if ((errno = pthread_rwlock_init(&e->rm_lock, NULL)) != 0) {
        return -1;
    }

    // Push the files to delete in rmfiles list
    if (export_load_rmfentry(e) != 0) {
        severe("export_load_rmfentry failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

void export_release(export_t * e) {
    close(e->fdstat);
    // TODO set members to clean values
}

int export_stat(export_t * e, estat_t * st) {
    int status = -1;
    struct statfs stfs;
    volume_stat_t vstat;
    START_PROFILING(export_stat);

    st->bsize = ROZOFS_BSIZE;
    if (statfs(e->root, &stfs) != 0)
        goto out;

    // may be ROZOFS_FILENAME_MAX should be stfs.f_namelen
    //st->namemax = stfs.f_namelen;
    st->namemax = ROZOFS_FILENAME_MAX;
    st->ffree = stfs.f_ffree;
    st->blocks = e->fstat.blocks;
    volume_stat(e->volume, &vstat);

    if (e->hquota > 0) {
        if (e->hquota < vstat.bfree) {
            st->bfree = e->hquota - st->blocks;
        } else {
            st->bfree = vstat.bfree - st->blocks;
        }
    } else {
        st->bfree = vstat.bfree;
    }
    //st->bfree = e->hquota > 0 && e->hquota < vstat.bfree ? e->hquota : vstat.bfree;
    // blocks store in export stat file is the number of currently stored blocks
    // blocks in estat_t is the total number of blocks (see struct statvfs)
    // rozofs does not have a constant total number of blocks
    // it depends on usage made of storage (through other services)
    st->blocks += st->bfree;
    st->files = e->fstat.files;

    status = 0;
out:
    STOP_PROFILING(export_stat);
    return status;
}

int export_lookup(export_t *e, fid_t pfid, char *name, mattr_t *attrs) {
    int status = -1;
    lv2_entry_t *plv2 = 0;
    lv2_entry_t *lv2 = 0;
    fid_t child_fid;
    uint32_t child_type;
    START_PROFILING(export_lookup);

    // get the lv2 parent
    if (!(plv2 = export_lookup_fid(e, pfid))) {
        goto out;
    }

    if (get_mdirentry(plv2->container.mdir.fdp, pfid, name, child_fid, &child_type) != 0) {
        goto out;
    }

    // get the lv2
    if (!(lv2 = export_lookup_fid(e, child_fid))) {
        goto out;
    }

    memcpy(attrs, &lv2->attributes, sizeof (mattr_t));

    status = 0;
out:
    STOP_PROFILING(export_lookup);
    return status;
}

int export_getattr(export_t *e, fid_t fid, mattr_t *attrs) {
    int status = -1;
    lv2_entry_t *lv2 = 0;
    START_PROFILING(export_getattr);

    if (!(lv2 = export_lookup_fid(e, fid))) {
        severe("export_getattr failed: %s", strerror(errno));
        goto out;
    }
    memcpy(attrs, &lv2->attributes, sizeof (mattr_t));

    status = 0;
out:
    STOP_PROFILING(export_getattr);
    return status;
}

int export_setattr(export_t *e, fid_t fid, mattr_t *attrs, int to_set) {
    int status = -1;
    lv2_entry_t *lv2 = 0;

    START_PROFILING(export_setattr);

    if (!(lv2 = export_lookup_fid(e, fid))) {
        goto out;
    }

    if ((to_set & EXPORT_SET_ATTR_SIZE) && S_ISREG(lv2->attributes.mode)) {
        if (attrs->size >= 0x20000000000LL) {
            errno = EFBIG;
            goto out;
        }

        uint64_t nrb_new = ((attrs->size + ROZOFS_BSIZE - 1) / ROZOFS_BSIZE);
        uint64_t nrb_old = ((lv2->attributes.size + ROZOFS_BSIZE - 1) /
                ROZOFS_BSIZE);

        if (lv2->attributes.size > attrs->size) {
            if (ftruncate(lv2->container.mreg.fdattrs, sizeof (mattr_t) + nrb_new * sizeof (dist_t)) != 0)
                goto out;
        } else {
            dist_t *empty;
            empty = xmalloc((nrb_new - nrb_old) * sizeof (dist_t));
            memset(empty, 0, (nrb_new - nrb_old) * sizeof (dist_t));
            if (mreg_write_dist(&lv2->container.mreg, nrb_old, nrb_new - nrb_old, empty) != 0) {
                free(empty);
                goto out;
            }
            free(empty);
        }
        if (export_update_blocks(e, ((int32_t) nrb_new - (int32_t) nrb_old))
                != 0)
            goto out;

        lv2->attributes.size = attrs->size;
    }

    if (to_set & EXPORT_SET_ATTR_MODE)
        lv2->attributes.mode = attrs->mode;
    if (to_set & EXPORT_SET_ATTR_UID)
        lv2->attributes.uid = attrs->uid;
    if (to_set & EXPORT_SET_ATTR_GID)
        lv2->attributes.gid = attrs->gid;
    //lv2->attributes.nlink = attrs->nlink;
    lv2->attributes.ctime = time(NULL);
    //lv2->attributes.atime = attrs->atime;
    //lv2->attributes.mtime = attrs->mtime;

    status = export_lv2_write_attributes(lv2);
out:
    STOP_PROFILING(export_setattr);
    return status;
}

int export_link(export_t *e, fid_t inode, fid_t newparent, char *newname, mattr_t *attrs) {
    int status = -1;
    lv2_entry_t *target = NULL;
    lv2_entry_t *plv2 = NULL;
    fid_t child_fid;
    uint32_t child_type;

    START_PROFILING(export_link);

    // Get the lv2 inode
    if (!(target = export_lookup_fid(e, inode)))
        goto out;

    // Verify that the target is not a directory
    if (S_ISDIR(target->attributes.mode)) {
        errno = EPERM;
        goto out;
    }

    // Get the lv2 parent
    if (!(plv2 = export_lookup_fid(e, newparent)))
        goto out;

    // Verify that the mdirentry does not already exist
    if (get_mdirentry(plv2->container.mdir.fdp, newparent, newname, child_fid, &child_type) != -1) {
        errno = EEXIST;
        goto out;
    }

    // Put the new mdirentry
    if (put_mdirentry(plv2->container.mdir.fdp, newparent, newname, target->attributes.fid, target->attributes.mode) != 0)
        goto out;

    // Update nlink and ctime for inode
    target->attributes.nlink++;
    target->attributes.ctime = time(NULL);

    // Write attributes of target
    if (export_lv2_write_attributes(target) != 0)
        goto out;

    // Update parent
    plv2->container.mdir.children++;
    plv2->attributes.mtime = plv2->attributes.ctime = time(NULL);

    // Write attributes of parents
    if (export_lv2_write_attributes(plv2) != 0)
        goto out;

    // Return attributes
    memcpy(attrs, &target->attributes, sizeof (mattr_t));

    status = 0;

out:
    STOP_PROFILING(export_link);
    return status;
}

int export_mknod(export_t *e, fid_t pfid, char *name, uint32_t uid,
        uint32_t gid, mode_t mode, mattr_t *attrs) {
    int status = -1;
    lv2_entry_t *plv2;
    fid_t node_fid;
    char node_path[PATH_MAX];
    mreg_t node_mreg;
    int xerrno = errno;
    uint32_t type;

    START_PROFILING(export_mknod);

    // get the lv2 parent
    if (!(plv2 = export_lookup_fid(e, pfid)))
        goto error;

    // check if exists
    if (get_mdirentry(plv2->container.mdir.fdp, pfid, name, node_fid, &type) == 0) {
        errno = EEXIST;
        goto error;
    }

    if (!S_ISREG(mode)) {
        errno = ENOTSUP;
        goto error;
    }

    // create the lv2 new file
    uuid_generate(node_fid);
    if (export_lv2_resolve_path(e, node_fid, node_path) != 0)
        goto error;

    if (mknod(node_path, S_IRWXU, 0) != 0)
        goto error;

    // generate attributes
    uuid_copy(attrs->fid, node_fid);
    if (volume_distribute(e->volume, &attrs->cid, attrs->sids) != 0)
        goto error;
    attrs->mode = mode;
    attrs->uid = uid;
    attrs->gid = gid;
    attrs->nlink = 1;
    if ((attrs->ctime = attrs->atime = attrs->mtime = time(NULL)) == -1)
        goto error;
    attrs->size = 0;

    // write attributes to mreg file
    if (mreg_open(&node_mreg, node_path) < 0)
        goto error;
    if (mreg_write_attributes(&node_mreg, attrs) != 0) {
        mreg_close(&node_mreg);
        goto error;
    }
    mreg_close(&node_mreg);

    // update the parent
    // add the new child to the parent
    if (put_mdirentry(plv2->container.mdir.fdp, pfid, name, node_fid, attrs->mode) != 0) {
        goto error;
    }
    plv2->container.mdir.children++;

    // update times of parent
    plv2->attributes.mtime = plv2->attributes.ctime = time(NULL);
    if (export_lv2_write_attributes(plv2) != 0) {
        goto error;
    }

    // update export files
    if (export_update_files(e, 1) != 0)
        goto error;

    status = 0;
    goto out;

error:
    xerrno = errno;
    if (xerrno != EEXIST) {
        unlink(node_path);
    }
    errno = xerrno;

out:
    STOP_PROFILING(export_mknod);
    return status;
}

int export_mkdir(export_t *e, fid_t pfid, char *name, uint32_t uid,
        uint32_t gid, mode_t mode, mattr_t * attrs) {
    int status = -1;
    lv2_entry_t *plv2;
    fid_t node_fid;
    char node_path[PATH_MAX];
    mdir_t node_mdir;
    int xerrno = errno;

    START_PROFILING(export_mkdir);

    // get the lv2 parent
    if (!(plv2 = export_lookup_fid(e, pfid)))
        goto error;

    // check if exists
    if (get_mdirentry(plv2->container.mdir.fdp, pfid, name, node_fid, &attrs->mode) == 0) {
        errno = EEXIST;
        goto error;
    }

    // create the lv2 new file
    uuid_generate(node_fid);
    if (export_lv2_resolve_path(e, node_fid, node_path) != 0)
        goto error;

    if (mkdir(node_path, S_IRWXU) != 0)
        goto error;

    // generate attributes
    uuid_copy(attrs->fid, node_fid);
    attrs->cid = 0;
    memset(attrs->sids, 0, ROZOFS_SAFE_MAX * sizeof (sid_t));
    attrs->mode = mode;
    attrs->uid = uid;
    attrs->gid = gid;
    attrs->nlink = 2;
    if ((attrs->ctime = attrs->atime = attrs->mtime = time(NULL)) == -1)
        goto error;
    attrs->size = ROZOFS_DIR_SIZE;
    node_mdir.children = 0;

    // write attributes to mdir file
    if (mdir_open(&node_mdir, node_path) < 0)
        goto error;
    if (mdir_write_attributes(&node_mdir, attrs) != 0) {
        mdir_close(&node_mdir);
        goto error;
    }

    // create "." ".." lv3 entries
    if (put_mdirentry(node_mdir.fdp, node_fid, ".", node_fid, S_IFDIR | S_IRWXU) != 0) {
        mdir_close(&node_mdir);
        return -1;
    }
    if (put_mdirentry(node_mdir.fdp, node_fid, "..", plv2->attributes.fid, S_IFDIR | S_IRWXU) != 0) {
        mdir_close(&node_mdir);
        return -1;
    }

    // update the parent
    // add the new child to the parent
    if (put_mdirentry(plv2->container.mdir.fdp, pfid, name, node_fid, attrs->mode) != 0) {
        goto error;
    }

    plv2->container.mdir.children++;
    plv2->attributes.nlink++;
    plv2->attributes.mtime = plv2->attributes.ctime = time(NULL);
    if (export_lv2_write_attributes(plv2) != 0)
        goto error;

    // update export files
    if (export_update_files(e, 1) != 0)
        goto error;

    mdir_close(&node_mdir);
    status =  0;
    goto out;

error:
    xerrno = errno;
    if (xerrno != EEXIST) {
        char fname[PATH_MAX];
        mdir_t node_mdir_del;
        // XXX: put version
        fid_t fid;
        uint32_t type;
        sprintf(fname, "%s/%s", node_path, MDIR_ATTRS_FNAME);
        unlink(fname);
        node_mdir_del.fdp = open(node_path, O_RDONLY, S_IRWXU);
        del_mdirentry(node_mdir_del.fdp, node_fid, ".", fid, &type);
        del_mdirentry(node_mdir_del.fdp, node_fid, "..", fid, &type);
        rmdir(node_path);
        mdir_close(&node_mdir_del);
    }
    errno = xerrno;
out:
    STOP_PROFILING(export_mkdir);
    return status;
}

int export_unlink(export_t * e, fid_t parent, char *name, fid_t fid) {
    int status = -1;
    lv2_entry_t *plv2, *lv2;
    fid_t child_fid;
    uint32_t child_type;
    uint16_t nlink = 0;
    char child_path[PATH_MAX];

    START_PROFILING(export_unlink);

    // Get the lv2 parent
    if (!(plv2 = export_lookup_fid(e, parent)))
        goto out;

    // Check if name exist
    if (get_mdirentry(plv2->container.mdir.fdp, parent, name, child_fid, &child_type) != 0)
        goto out;

    if (S_ISDIR(child_type)) {
        errno = EISDIR;
        goto out;
    }

    // Delete the mdirentry if exist
    if (del_mdirentry(plv2->container.mdir.fdp, parent, name, child_fid, &child_type) != 0)
        goto out;

    // Resolve path for the file to delete
    if (export_lv2_resolve_path(e, child_fid, child_path) != 0)
        goto out;

    // Get mattrs of child to delete
    if (!(lv2 = export_lookup_fid(e, child_fid)))
        goto out;

    // Get nlink
    nlink = lv2->attributes.nlink;

    // 2 cases:
    // nlink > 1, it's a hardlink -> not delete the lv2 file
    // nlink=1, it's not a harlink -> put the lv2 file on trash directory

    // Not a hardlink
    if (nlink == 1) {

        if (lv2->attributes.size > 0 && S_ISREG(lv2->attributes.mode)) {

            char rm_path[PATH_MAX];
            char fid_str[37];
            uuid_unparse(lv2->attributes.fid, fid_str);
            sprintf(rm_path, "%s/%s/%s", e->root, TRASH_DNAME, fid_str);

            if (rename(child_path, rm_path) == -1) {
                severe("rename for trash (%s to %s) failed: %s", child_path, rm_path, strerror(errno));
                goto out;
            }

            // Preparation of the rmfentry
            rmfentry_t *rmfe = xmalloc(sizeof (rmfentry_t));
            memcpy(rmfe->fid, lv2->attributes.fid, sizeof (fid_t));
            memcpy(rmfe->sids, lv2->attributes.sids, sizeof (sid_t) * ROZOFS_SAFE_MAX);
            list_init(&rmfe->list);

            // Adding the rmfentry to the list of files to delete
            if ((errno = pthread_rwlock_wrlock(&e->rm_lock)) != 0)
                goto out;
            list_push_back(&e->rmfiles, &rmfe->list);
            if ((errno = pthread_rwlock_unlock(&e->rm_lock)) != 0)
                goto out;

            // Update the nb. of blocks
            if (export_update_blocks(e, -(((int64_t) lv2->attributes.size + ROZOFS_BSIZE - 1) / ROZOFS_BSIZE)) != 0)
                goto out;
        } else {
            if (unlink(child_path) != 0)
                goto out;
        }

        // Return the fid of deleted file
        memcpy(fid, child_fid, sizeof (fid_t));

        // Update export files
        if (export_update_files(e, -1) != 0)
            goto out;

        // Remove from the cache (will be closed and freed)
        lv2_cache_del(e->lv2_cache, child_fid);
    }
    // It's a hardlink
    if (nlink > 1) {
        lv2->attributes.nlink--;
        lv2->attributes.ctime = time(NULL);
        export_lv2_write_attributes(lv2);
        // Return a empty fid because no inode has been deleted
        memset(fid, 0, sizeof (fid_t));
    }

    // Update parent
    plv2->attributes.mtime = plv2->attributes.ctime = time(NULL);
    plv2->container.mdir.children--;

    // Write attributes of parents
    if (export_lv2_write_attributes(plv2) != 0)
        goto out;

    status = 0;

out:
    STOP_PROFILING(export_unlink);
    return status;
}

static int init_storages_cnx(volume_t *volume, list_t *list) {
    list_t *p, *q;
    int status = -1;
    DEBUG_FUNCTION;

    if ((errno = pthread_rwlock_tryrdlock(&volume->lock)) != 0) {

        severe("init_storages_cnx failed: can't lock volume %d.", volume->vid);
        goto out;
    }

    list_for_each_forward(p, &volume->clusters) {

        cluster_t *cluster = list_entry(p, cluster_t, list);

        list_for_each_forward(q, &cluster->storages) {

            volume_storage_t *vs = list_entry(q, volume_storage_t, list);

            mclient_t * sclt = (mclient_t *) xmalloc(sizeof (mclient_t));

            strcpy(sclt->host, vs->host);
            sclt->sid = vs->sid;

            if (mclient_initialize(sclt) != 0) {
                warning("failed to join: %s,  %s", vs->host, strerror(errno));
            }

            cnxentry_t *cnx_entry = (cnxentry_t *) xmalloc(sizeof (cnxentry_t));
            cnx_entry->cnx = sclt;
            // Add this export to the list of exports
            list_push_back(list, &cnx_entry->list);

        }
    }

    if ((errno = pthread_rwlock_unlock(&volume->lock)) != 0) {
        severe("init_storages_cnx failed: can't unlock volume %d.", volume->vid);
        goto out;
    }
    status = 0;
out:

    return status;
}

static mclient_t * lookup_cnx(list_t *list, sid_t sid) {

    list_t *p;
    DEBUG_FUNCTION;

    list_for_each_forward(p, list) {
        cnxentry_t *cnx_entry = list_entry(p, cnxentry_t, list);

        if (sid == cnx_entry->cnx->sid) {
            return cnx_entry->cnx;
            break;
        }
    }

    severe("lookup_cnx failed : storage connexion (sid: %u) not found", sid);

    errno = EINVAL;

    return NULL;
}

static void release_storages_cnx(list_t *list) {

    list_t *p, *q;
    DEBUG_FUNCTION;

    list_for_each_forward_safe(p, q, list) {

        cnxentry_t *cnx_entry = list_entry(p, cnxentry_t, list);
        mclient_release(cnx_entry->cnx);
        list_remove(p);
        free(cnx_entry);
    }
}

int export_rm_bins(export_t * e) {
    int status = -1;
    int cnt = 0;
    int release = 0;
    int limit_rm_files = RM_FILES_MAX;
    int curr_rm_files = 0;
    DEBUG_FUNCTION;
    list_t connexions;

    // If the list is no empty
    // Create a list of connections to storages
    if (!list_empty(&e->rmfiles)) {
        // Init list of connexions
        list_init(&connexions);
        release = 1;
        if (init_storages_cnx(e->volume, &connexions) != 0) {
            severe("init_storages_cnx failed: %s", strerror(errno));
            goto out;
        }
    }

    // For each file to delete
    while (!list_empty(&e->rmfiles) && curr_rm_files < limit_rm_files) {

        // Remove entry for the list of files to delete
        if ((errno = pthread_rwlock_trywrlock(&e->rm_lock)) != 0) {
            severe("pthread_rwlock_trywrlock failed");
            goto out;
        }

        rmfentry_t *entry = list_first_entry(&e->rmfiles, rmfentry_t, list);
        list_remove(&entry->list);

        if ((errno = pthread_rwlock_unlock(&e->rm_lock)) != 0) {
            severe("pthread_rwlock_unlock failed");
            goto out;
        }

        sid_t *it = entry->sids;
        cnt = 0; // Nb. of bins removed

        // For each storage
        while (it != entry->sids + rozofs_safe) {

            // If this storage have bins for this file
            if (*it != 0) {
                // Get the connexion for this storage
                mclient_t* stor = lookup_cnx(&connexions, *it);

                // Send request to storage for remove bins
                if (mclient_remove(stor, entry->fid) != 0) {
                    severe("storageclt_remove failed: (sid: %u) %s", stor->sid, strerror(errno));
                } else {
                    // If storageclt_remove works
                    // Replace sid for this storage by 0
                    // Increment nb. of bins deleted
                    *it = 0;
                    cnt++;
                }

            } else {
                // Bins are already deleted for this storage
                cnt++;
            }
            it++;
        }

        // If all bins are deleted
        // Remove the file from trash
        if (cnt == rozofs_safe) {
            char path[PATH_MAX];
            // In case of rename it's not neccessary to remove trash file
            char fid_str[37];
            uuid_unparse(entry->fid, fid_str);
            sprintf(path, "%s/%s/%s", e->root, TRASH_DNAME, fid_str);

            if (unlink(path) == -1) {
                severe("unlink failed: unlink file %s failed: %s", path, strerror(errno));
                goto out;
            }
            free(entry);
        } else { // If NO all bins are deleted

            // Repush entry in the list of files to delete
            if ((errno = pthread_rwlock_wrlock(&e->rm_lock)) != 0) {
                severe("pthread_rwlock_trywrlock failed");
                goto out;
            }

            list_push_back(&e->rmfiles, &entry->list);

            if ((errno = pthread_rwlock_unlock(&e->rm_lock)) != 0) {
                severe("pthread_rwlock_unlock failed");
                goto out;
            }
        }
        curr_rm_files++;
    }

    status = 0;
out:
    if (release == 1) {
        // Release storage connexions
        release_storages_cnx(&connexions);
    }
    return status;
}

int export_rmdir(export_t *e, fid_t pfid, char *name, fid_t fid) {
    int status = -1;
    lv2_entry_t *plv2;
    lv2_entry_t *lv2;
    fid_t fake_fid;
    fid_t dot_fid;
    fid_t dot_dot_fid;
    uint32_t fake_type;
    uint32_t dot_type;
    uint32_t dot_dot_type;
    char lv2_path[PATH_MAX];
    char lv3_path[PATH_MAX];

    START_PROFILING(export_rmdir);

    // get the lv2 parent
    if (!(plv2 = export_lookup_fid(e, pfid)))
        goto out;

    // get the fid according to name
    if (get_mdirentry(plv2->container.mdir.fdp, pfid, name, fid, &fake_type) != 0)
        goto out;

    // get the lv2
    if (!(lv2 = export_lookup_fid(e, fid)))
        goto out;

    // sanity checks (is a directory and lv3 is empty)
    if (!S_ISDIR(lv2->attributes.mode)) {
        errno = ENOTDIR;
        goto out;
    }

    if (lv2->container.mdir.children != 0) {
        errno = ENOTEMPTY;
        goto out;
    }

    // XXX starting from here, any failure will leads to inconsistent state.
    if (del_mdirentry(lv2->container.mdir.fdp, fid, ".", dot_fid, &dot_type) != 0)
        goto out;
    if (del_mdirentry(lv2->container.mdir.fdp, fid, "..", dot_dot_fid, &dot_dot_type) != 0)
        goto out;

    // remove from the cache (will be closed and freed)
    lv2_cache_del(e->lv2_cache, fid);

    // update parent
    if (del_mdirentry(plv2->container.mdir.fdp, pfid, name, fake_fid, &fake_type) != 0)
        goto out;

    plv2->container.mdir.children--;
    plv2->attributes.nlink--;
    plv2->attributes.mtime = plv2->attributes.ctime = time(NULL);
    if (export_lv2_write_attributes(plv2) != 0)
        goto out;

    // update export nb files
    if (export_update_files(e, -1) != 0)
        goto out;

    // remove lv2
    if (export_lv2_resolve_path(e, fid, lv2_path) != 0)
        goto out;

    sprintf(lv3_path, "%s/%s", lv2_path, MDIR_ATTRS_FNAME);

    if (unlink(lv3_path) != 0)
        goto out;

    if (rmdir(lv2_path) != 0)
        goto out;

    status = 0;
out:
    STOP_PROFILING(export_rmdir);
    return status;
}

int export_symlink(export_t * e, char *link, fid_t pfid, char *name,
        mattr_t * attrs) {

    int status = -1;
    lv2_entry_t *plv2;
    fid_t node_fid;
    char node_path[PATH_MAX];
    mslnk_t node_mslnk;
    int xerrno = errno;

    START_PROFILING(export_symlink);

    // get the lv2 parent
    if (!(plv2 = export_lookup_fid(e, pfid)))
        goto error;

    // check if exists
    if (get_mdirentry(plv2->container.mdir.fdp, pfid, name, node_fid, &attrs->mode) == 0) {
        errno = EEXIST;
        goto error;
    }

    // create the lv2 new file
    uuid_generate(node_fid);
    if (export_lv2_resolve_path(e, node_fid, node_path) != 0)
        goto error;

    if (mknod(node_path, S_IRWXU, 0) != 0)
        goto error;

    // generate attributes
    uuid_copy(attrs->fid, node_fid);
    attrs->cid = 0;
    memset(attrs->sids, 0, ROZOFS_SAFE_MAX * sizeof (sid_t));
    attrs->mode = S_IFLNK | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP |
            S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH;
    attrs->uid = getuid();
    attrs->gid = getgid();
    attrs->nlink = 1;
    if ((attrs->ctime = attrs->atime = attrs->mtime = time(NULL)) == -1)
        goto error;
    attrs->size = strlen(link);

    // write attributes and link
    if (mslnk_open(&node_mslnk, node_path) < 0)
        goto error;
    if (mslnk_write_attributes(&node_mslnk, attrs) != 0) {
        mslnk_close(&node_mslnk);
        goto error;
    }
    if (mslnk_write_link(&node_mslnk, link) != 0) {
        mslnk_close(&node_mslnk);
        goto error;
    }

    mslnk_close(&node_mslnk);

    // update the parent
    // add the new child to the parent
    if (put_mdirentry(plv2->container.mdir.fdp, pfid, name, node_fid, attrs->mode) != 0)
        goto error;
    plv2->container.mdir.children++;
    // update times of parent
    plv2->attributes.mtime = plv2->attributes.ctime = time(NULL);
    if (export_lv2_write_attributes(plv2) != 0)
        goto error;

    // update export files
    if (export_update_files(e, 1) != 0)
        goto error;

    status = 0;
    goto out;

error:
    xerrno = errno;
    if (xerrno != EEXIST) {
        unlink(node_path);
    }
    errno = xerrno;

out:
    STOP_PROFILING(export_symlink);
    return status;
}

int export_readlink(export_t *e, fid_t fid, char *link) {
    int status = -1;
    lv2_entry_t *lv2 = 0;
    START_PROFILING(export_readlink);

    if (!(lv2 = export_lookup_fid(e, fid))) {
        goto out;
    }
    status = mslnk_read_link(&lv2->container.mslnk, link);
out:
    STOP_PROFILING(export_readlink);
    return status;
}

int export_rename(export_t *e, fid_t pfid, char *name, fid_t npfid, char *newname, fid_t fid) {
    int status = -1;
    lv2_entry_t *lv2_old_parent = 0;
    lv2_entry_t *lv2_new_parent = 0;
    lv2_entry_t *lv2_to_rename = 0;
    lv2_entry_t *lv2_to_replace = 0;
    fid_t fid_to_rename;
    uint32_t type_to_rename;
    fid_t fid_to_replace;
    uint32_t type_to_replace;

    START_PROFILING(export_rename);

    // Get the lv2 entry of old parent
    if (!(lv2_old_parent = export_lookup_fid(e, pfid))) {
        goto out;
    }

    // Verify that the old parent is a directory
    if (!S_ISDIR(lv2_old_parent->attributes.mode)) {
        errno = ENOTDIR;
        goto out;
    }

    // Check if the file/dir to rename exist
    if (get_mdirentry(lv2_old_parent->container.mdir.fdp, pfid, name, fid_to_rename, &type_to_rename) != 0)
        goto out;

    // Get the lv2 entry of file/dir to rename
    if (!(lv2_to_rename = export_lookup_fid(e, fid_to_rename)))
        goto out;

    // Get the lv2 entry of newparent
    if (!(lv2_new_parent = export_lookup_fid(e, npfid)))
        goto out;

    // Verify that the new parent is a directory
    if (!S_ISDIR(lv2_new_parent->attributes.mode)) {
        errno = ENOTDIR;
        goto out;
    }

    memset(fid, 0, sizeof (fid_t));

    // Get the old mdirentry if exist
    if (get_mdirentry(lv2_new_parent->container.mdir.fdp, npfid, newname, fid_to_replace, &type_to_replace) == 0) {

        // We must delete the old entry

        // Get mattrs of entry to delete
        if (!(lv2_to_replace = export_lookup_fid(e, fid_to_replace)))
            goto out;

        // The entry (to replace) is an existing directory
        if (S_ISDIR(lv2_to_replace->attributes.mode)) {

            // The entry to rename must be a directory
            if (!S_ISDIR(lv2_to_rename->attributes.mode)) {
                errno = EISDIR;
                goto out;
            }

            // The entry to replace must be a enpty directory
            if (lv2_to_replace->container.mdir.children != 0) {
                errno = ENOTEMPTY;
                goto out;
            }

            // Delete mdirentry . and .. of dir to replace
            fid_t dot_fid, dot_dot_fid;
            uint32_t dot_type, dot_dot_type;

            if (del_mdirentry(lv2_to_replace->container.mdir.fdp, fid_to_replace, ".", dot_fid, &dot_type) != 0)
                goto out;
            if (del_mdirentry(lv2_to_replace->container.mdir.fdp, fid_to_replace, "..", dot_dot_fid, &dot_dot_type) != 0)
                goto out;

            // Update parent directory
            lv2_new_parent->attributes.nlink--;
            lv2_new_parent->container.mdir.children--;

            // We'll write attributes of parents after

            // Update export files
            if (export_update_files(e, -1) != 0)
                goto out;

            char lv2_path[PATH_MAX];
            char lv3_path[PATH_MAX];

            if (export_lv2_resolve_path(e, lv2_to_replace->attributes.fid, lv2_path) != 0)
                goto out;

            sprintf(lv3_path, "%s/%s", lv2_path, MDIR_ATTRS_FNAME);

            if (unlink(lv3_path) != 0)
                goto out;

            if (rmdir(lv2_path) != 0)
                goto out;

            // Remove the dir to replace from the cache (will be closed and freed)
            lv2_cache_del(e->lv2_cache, fid_to_replace);

            // Return the fid of deleted directory
            memcpy(fid, fid_to_replace, sizeof (fid_t));

        } else {
            // The entry (to replace) is an existing file
            if (S_ISREG(lv2_to_replace->attributes.mode) || S_ISLNK(lv2_to_replace->attributes.mode)) {
                // Get nlink
                uint16_t nlink = lv2_to_replace->attributes.nlink;

                // 2 cases:
                // nlink > 1, it's a hardlink -> not delete the lv2 file
                // nlink=1, it's not a harlink -> put the lv2 file on trash directory

                // Not a hardlink
                if (nlink == 1) {

                    char old_path[PATH_MAX];

                    // Resolve path for the node to delete
                    if (export_lv2_resolve_path(e, fid_to_replace, old_path) != 0)
                        goto out;


                    if (lv2_to_replace->attributes.size > 0 && S_ISREG(lv2_to_replace->attributes.mode)) {

                        char rm_path[PATH_MAX];
                        char fid_str[37];
                        uuid_unparse(lv2_to_replace->attributes.fid, fid_str);
                        sprintf(rm_path, "%s/%s/%s", e->root, TRASH_DNAME, fid_str);

                        if (rename(old_path, rm_path) == -1) {
                            severe("rename for trash (%s to %s) failed: %s", old_path, rm_path, strerror(errno));
                            goto out;
                        }

                        // Preparation of the rmfentry
                        rmfentry_t *rmfe = xmalloc(sizeof (rmfentry_t));
                        memcpy(rmfe->fid, lv2_to_replace->attributes.fid, sizeof (fid_t));
                        memcpy(rmfe->sids, lv2_to_replace->attributes.sids, sizeof (sid_t) * ROZOFS_SAFE_MAX);
                        list_init(&rmfe->list);

                        // Adding the rmfentry to the list of files to delete
                        if ((errno = pthread_rwlock_wrlock(&e->rm_lock)) != 0)
                            goto out;
                        list_push_back(&e->rmfiles, &rmfe->list);
                        if ((errno = pthread_rwlock_unlock(&e->rm_lock)) != 0)
                            goto out;

                        // Update the nb. of blocks
                        if (export_update_blocks(e, -(((int64_t) lv2_to_replace->attributes.size + ROZOFS_BSIZE - 1) / ROZOFS_BSIZE)) != 0)
                            goto out;

                    } else {
                        if (unlink(old_path) != 0)
                            goto out;
                    }

                    // Update export files
                    if (export_update_files(e, -1) != 0)
                        goto out;

                    // Remove from the cache (will be closed and freed)
                    lv2_cache_del(e->lv2_cache, fid_to_replace);

                    // Return the fid of deleted directory
                    memcpy(fid, fid_to_replace, sizeof (fid_t));
                }

                // It's a hardlink
                if (nlink > 1) {
                    lv2_to_replace->attributes.nlink--;
                    export_lv2_write_attributes(lv2_to_replace);
                    // Return a empty fid because no inode has been deleted
                    memset(fid, 0, sizeof (fid_t));
                }
                lv2_new_parent->container.mdir.children--;
            }
        }
    }

    // Put the mdirentry
    if (put_mdirentry(lv2_new_parent->container.mdir.fdp, npfid, newname, lv2_to_rename->attributes.fid, lv2_to_rename->attributes.mode) != 0) {
        goto out;
    }

    // Delete the mdirentry
    if (del_mdirentry(lv2_old_parent->container.mdir.fdp, pfid, name, fid_to_rename, &type_to_rename) != 0)
        goto out;

    if (memcmp(pfid, npfid, sizeof (fid_t)) != 0) {

        lv2_new_parent->container.mdir.children++;
        lv2_old_parent->container.mdir.children--;

        if (S_ISDIR(lv2_to_rename->attributes.mode)) {
            lv2_new_parent->attributes.nlink++;
            lv2_old_parent->attributes.nlink--;

            // If the node to rename is a directory
            // We must change the subdirectory '..'
            if (put_mdirentry(lv2_to_rename->container.mdir.fdp, fid_to_rename, "..", lv2_new_parent->attributes.fid, lv2_new_parent->attributes.mode) != 0) {
                goto out;
            }

        }

        lv2_new_parent->attributes.mtime = lv2_new_parent->attributes.ctime = time(NULL);
        lv2_old_parent->attributes.mtime = lv2_old_parent->attributes.ctime = time(NULL);

        if (export_lv2_write_attributes(lv2_new_parent) != 0)
            goto out;

        if (export_lv2_write_attributes(lv2_old_parent) != 0)
            goto out;
    } else {

        lv2_new_parent->attributes.mtime = lv2_new_parent->attributes.ctime = time(NULL);

        if (export_lv2_write_attributes(lv2_new_parent) != 0)
            goto out;
    }

    // Update ctime of renamed file/directory
    lv2_to_rename->attributes.ctime = time(NULL);

    // Write attributes of renamed file
    if (export_lv2_write_attributes(lv2_to_rename) != 0)
        goto out;

    status = 0;

out:
    STOP_PROFILING(export_rename);
    return status;
}

int64_t export_read(export_t * e, fid_t fid, uint64_t offset, uint32_t len,
        uint64_t * first_blk, uint32_t * nb_blks) {
    lv2_entry_t *lv2 = NULL;
    int64_t length = -1;
    uint64_t i_first_blk = 0;
    uint64_t i_last_blk = 0;
    uint32_t i_nb_blks = 0;

    START_PROFILING(export_read);

    // Get the lv2 entry
    if (!(lv2 = export_lookup_fid(e, fid))) {
        goto error;
    }

    // EOF ?
    if (offset > lv2->attributes.size) {
        errno = 0;
        goto error;
    }

    // Length to read
    length = (offset + len < lv2->attributes.size ? len : lv2->attributes.size - offset);
    // Nb. of the first block to read
    i_first_blk = offset / ROZOFS_BSIZE;
    // Nb. of the last block to read
    i_last_blk = (offset + length) / ROZOFS_BSIZE + ((offset + length) % ROZOFS_BSIZE == 0 ? -1 : 0);
    // Nb. of blocks to read
    i_nb_blks = (i_last_blk - i_first_blk) + 1;

    *first_blk = i_first_blk;
    *nb_blks = i_nb_blks;

    // Managed access time
    if ((lv2->attributes.atime = time(NULL)) == -1)
        goto error;

    // Write attributes of file
    if (export_lv2_write_attributes(lv2) != 0)
        goto error;

    // Return the length that can be read
    goto out;

error:
    length = -1;
out:
    STOP_PROFILING(export_read);
    return length;
}

int export_read_block(export_t *e, fid_t fid, bid_t bid, uint32_t n, dist_t* d) {
    int status = -1;
    lv2_entry_t *lv2 = NULL;

    START_PROFILING(export_read_block);

    // Get the lv2 entry
    if (!(lv2 = export_lookup_fid(e, fid)))
        goto out;

    status = mreg_read_dist(&lv2->container.mreg, bid, n, d);
out:
    STOP_PROFILING(export_read_block);
    return status;
}

/* not used anymore
int64_t export_write(export_t *e, fid_t fid, uint64_t off, uint32_t len) {
    lv2_entry_t *lv2;

    if (!(lv2 = export_lookup_fid(e, fid))) {
        return -1;
    }

    if (off + len > lv2->attributes.size) {
        // Don't skip intermediate computation to keep ceil rounded
        uint64_t nbold = (lv2->attributes.size + ROZOFS_BSIZE - 1) / ROZOFS_BSIZE;
        uint64_t nbnew = (off + len + ROZOFS_BSIZE - 1) / ROZOFS_BSIZE;

        if (export_update_blocks(e, nbnew - nbold) != 0)
            return -1;

        lv2->attributes.size = off + len;
    }

    lv2->attributes.mtime = lv2->attributes.ctime = time(NULL);

    if (export_lv2_write_attributes(lv2) != 0)
        return -1;

    return len;
}*/

int64_t export_write_block(export_t *e, fid_t fid, uint64_t bid, uint32_t n,
        dist_t d, uint64_t off, uint32_t len) {
    int64_t length = -1;
    lv2_entry_t *lv2 = NULL;
    dist_t *dist = NULL;
    int i = 0;

    START_PROFILING(export_write_block);

    // Get the lv2 entry
    if (!(lv2 = export_lookup_fid(e, fid)))
        goto out;

    /// Write distribution
    dist = xmalloc(n * sizeof (dist_t));
    for (i = 0; i < n; i++) {
        memcpy(dist + i, &d, sizeof (dist_t));
    }
    if (mreg_write_dist(&lv2->container.mreg, bid, n, dist) != 0) {
        free(dist);
        goto out;
    }
    free(dist);

    // Update size of file
    if (off + len > lv2->attributes.size) {
        // Don't skip intermediate computation to keep ceil rounded
        uint64_t nbold = (lv2->attributes.size + ROZOFS_BSIZE - 1) / ROZOFS_BSIZE;
        uint64_t nbnew = (off + len + ROZOFS_BSIZE - 1) / ROZOFS_BSIZE;

        if (export_update_blocks(e, nbnew - nbold) != 0)
            goto out;

        lv2->attributes.size = off + len;
    }

    // Update mtime and ctime
    lv2->attributes.mtime = lv2->attributes.ctime = time(NULL);
    if (export_lv2_write_attributes(lv2) != 0)
        goto out;

    length = len;
out:
    STOP_PROFILING(export_write_block);
    return length;
}

int export_readdir(export_t * e, fid_t fid, uint64_t * cookie,
        child_t ** children, uint8_t *eof) {
    int status = -1;
    lv2_entry_t *parent = NULL;

    START_PROFILING(export_readdir);

    // Get the lv2 inode
    if (!(parent = export_lookup_fid(e, fid)))
        goto out;

    // Verify that the target is a directory
    if (!S_ISDIR(parent->attributes.mode)) {
        errno = ENOTDIR;
        goto out;
    }

    // List directory
    if (list_mdirentries(parent->container.mdir.fdp, fid, children, cookie, eof) != 0) {
        goto out;
    }

    // Update atime of parent
    parent->attributes.atime = time(NULL);
    if (export_lv2_write_attributes(parent) != 0)
        goto out;

    status = 0;
out:
    STOP_PROFILING(export_readdir);
    return status;
}

ssize_t export_getxattr(export_t *e, fid_t fid, const char *name, void *value, size_t size) {
    ssize_t status = -1;
    lv2_entry_t *lv2 = 0;

    START_PROFILING(export_getxattr);
    
    if (!(lv2 = export_lookup_fid(e, fid))) {
        severe("export_getattr failed: %s", strerror(errno));
        goto out;
    }

    if ((status = export_lv2_get_xattr(lv2, name, value, size)) < 0) {
        goto out;
    }

out:
    STOP_PROFILING(export_getxattr);
    return status;
}

int export_setxattr(export_t *e, fid_t fid, char *name, const void *value, size_t size, int flags) {
    int status = -1;
    lv2_entry_t *lv2 = 0;

    START_PROFILING(export_setxattr);
    
    if (!(lv2 = export_lookup_fid(e, fid))) {
        severe("export_getattr failed: %s", strerror(errno));
        goto out;
    }

    if ((status = export_lv2_set_xattr(lv2, name, value, size, flags)) != 0) {
        goto out;
    }

    status = 0;
out:
    STOP_PROFILING(export_setxattr);
    return status;
}

int export_removexattr(export_t *e, fid_t fid, char *name) {
    int status = -1;
    lv2_entry_t *lv2 = 0;
    
    START_PROFILING(export_removexattr);

    if (!(lv2 = export_lookup_fid(e, fid))) {
        severe("export_getattr failed: %s", strerror(errno));
        goto out;
    }

    if ((status = export_lv2_remove_xattr(lv2, name)) != 0) {
        goto out;
    }

    status = 0;
out:
    STOP_PROFILING(export_removexattr);
    return status;
}

ssize_t export_listxattr(export_t *e, fid_t fid, void *list, size_t size) {
    ssize_t status = -1;
    lv2_entry_t *lv2 = 0;
    
    START_PROFILING(export_listxattr);

    if (!(lv2 = export_lookup_fid(e, fid))) {
        severe("export_getattr failed: %s", strerror(errno));
        goto out;
    }

    if ((status = export_lv2_list_xattr(lv2, list, size)) < 0) {
        goto out;
    }

out:
    STOP_PROFILING(export_listxattr);
    return status;
}

/*
int export_open(export_t * e, fid_t fid) {
    int flag;

    flag = O_RDWR;

    if (!(mfe = export_get_mfentry_by_id(e, fid))) {
        severe("export_open failed: export_get_mfentry_by_id failed");
        goto out;
    }

    if (mfe->fd == -1) {
        if ((mfe->fd = open(mfe->path, flag)) < 0) {
            severe("export_open failed for file %s: %s", mfe->path,
                    strerror(errno));
            goto out;
        }
    }

    mfe->cnt++;

    status = 0;
out:

    return status;
}

int export_close(export_t * e, fid_t fid) {
    int status = -1;
    mfentry_t *mfe = 0;
    DEBUG_FUNCTION;

    if (!(mfe = export_get_mfentry_by_id(e, fid))) {
        severe("export_close failed: export_get_mfentry_by_id failed");
        goto out;
    }

    if (mfe->cnt == 1) {
        if (close(mfe->fd) != 0) {
            goto out;
        }
        mfe->fd = -1;
    }

    mfe->cnt--;

    status = 0;
out:
    return status;
}
 */
