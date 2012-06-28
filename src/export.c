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

#include "config.h"
#include "log.h"
#include "xmalloc.h"
#include "list.h"
#include "export.h"
#include "rozofs.h"
#include "volume.h"
#include "storageclt.h"

#define EHSIZE 8192
#define E_CSIZE 65536

#define EBLOCKSKEY	"user.rozofs.export.blocks"
#define ETRASHUUID	"user.rozofs.export.trashid"
#define EFILESKEY	"user.rozofs.export.files"
#define EVERSIONKEY	"user.rozofs.export.version"
#define EATTRSTKEY	"user.rozofs.export.file.attrs"

static inline char *export_trash_map(export_t * e, fid_t fid, char *path) {
    char fid_str[37];
    uuid_unparse(fid, fid_str);
    strcpy(path, e->root);
    strcat(path, "/");
    strcat(path, e->trashname);
    strcat(path, "/");
    strcat(path, fid_str);
    return path;
}

static int export_check_root(const char *root) {
    int status = -1;
    struct stat st;

    DEBUG_FUNCTION;

    if (stat(root, &st) == -1) {
        goto out;
    }
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        goto out;
    }
    status = 0;
out:
    return status;
}

// Just check if VERSION is set.
static int export_check_setup(const char *root) {
    char version[20];

    DEBUG_FUNCTION;

    return getxattr(root, EVERSIONKEY, &version, 20) == -1 ? -1 : 0;
}

typedef struct mfentry {
    struct mfentry *parent; // Parent
    fid_t pfid;             // Parent UUID XXX Why since we have it in parent ??
    char *path;             // Absolute path on underlying fs
    char *name;             // Name of file
    int fd;                 // File descriptor
    uint16_t cnt;           // Open counter
    mattr_t attrs;          // meta file attr
    list_t list;
} mfentry_t;

static int mfentry_initialize(mfentry_t *mfe, mfentry_t *parent,
        const char *name, char *path, mattr_t *mattrs) {
    DEBUG_FUNCTION;

    mfe->parent = parent;
    mfe->path = xstrdup(path);
    mfe->name = xstrdup(name);

    uuid_clear(mfe->pfid);
    if (parent != NULL) {
        memcpy(mfe->pfid, parent->attrs.fid, sizeof (fid_t));
    }
    // Open the fd is not necessary now
    mfe->fd = -1;
    // The counter is initialized to zero
    mfe->cnt = 0;
    if (mattrs != NULL)
        memcpy(&mfe->attrs, mattrs, sizeof (mattr_t));
    list_init(&mfe->list);

    return 0;
}

static void mfentry_release(mfentry_t *mfe) {
    DEBUG_FUNCTION;
    if (mfe) {
        if (mfe->fd != -1)
            close(mfe->fd);
        if (mfe->path)
            free(mfe->path);
        if (mfe->name)
            free(mfe->name);
    }
}

static int mfentry_create(mfentry_t * mfe) {
    return setxattr(mfe->path, EATTRSTKEY, &(mfe->attrs), sizeof (mattr_t),
            XATTR_CREATE);
}

static int mfentry_read(mfentry_t *mfe) {
    return (getxattr(mfe->path, EATTRSTKEY, &(mfe->attrs),
            sizeof (mattr_t)) < 0 ? -1 : 0);
}

static int mfentry_write(mfentry_t * mfe) {
    return setxattr(mfe->path, EATTRSTKEY, &(mfe->attrs), sizeof (mattr_t),
            XATTR_REPLACE);
}

static uint32_t mfentry_hash_fid(void *key) {
    uint32_t hash = 0;
    uint8_t *c;

    for (c = key; c != key + 16; c++)
        hash = *c + (hash << 6) + (hash << 16) - hash;
    return hash;
}

static uint32_t mfentry_hash_fid_name(void *key) {
    mfentry_t *mfe = (mfentry_t *) key;
    uint32_t hash = 0;
    uint8_t *c;
    char *d;

    for (c = mfe->pfid; c != mfe->pfid + 16; c++)
        hash = *c + (hash << 6) + (hash << 16) - hash;
    for (d = mfe->name; *d != '\0'; d++)
        hash = *d + (hash << 6) + (hash << 16) - hash;
    return hash;
}

static int mfentry_cmp_fid(void *k1, void *k2) {
    return memcmp(k1, k2, sizeof (fid_t));
}

static int mfentry_cmp_fid_name(void *k1, void *k2) {

    mfentry_t *sk1 = (mfentry_t *) k1;
    mfentry_t *sk2 = (mfentry_t *) k2;

    if ((uuid_compare(sk1->pfid, sk2->pfid) == 0) &&
            (strcmp(sk1->name, sk2->name) == 0)) {
        return 0;
    } else {
        return 1;
    }
}

typedef struct rmfentry {
    fid_t fid;
    sid_t sids[ROZOFS_SAFE_MAX];
    list_t list;
} rmfentry_t;

typedef struct cnxentry {
    storageclt_t *cnx;
    list_t list;
} cnxentry_t;

static int export_load_rmfentry(export_t * e) {
    int status = -1;
    DIR *dd = NULL;
    struct dirent *dp;
    rmfentry_t *rmfe = NULL;
    char trash_path[PATH_MAX + FILENAME_MAX + 1];

    DEBUG_FUNCTION;

    strcpy(trash_path, e->root);
    strcat(trash_path, "/");
    strcat(trash_path, e->trashname);

    if ((dd = opendir(trash_path)) == NULL) {
        severe("export_load_rmfentry failed: opendir (trash directory) failed: %s",
                strerror(errno));
        goto out;
    }

    while ((dp = readdir(dd)) != NULL) {

        if ((strcmp(dp->d_name, ".") == 0) || (strcmp(dp->d_name, "..") == 0)) {
            continue;
        }

        char rm_path[PATH_MAX + FILENAME_MAX + 1];
        uuid_t rmfid;
        mattr_t attrs;
        uuid_parse(dp->d_name, rmfid);

        if (getxattr(export_trash_map(e, rmfid, rm_path), EATTRSTKEY, &attrs,
                sizeof (mattr_t)) == -1) {
            severe("export_load_rmfentry failed: getxattr for file %s failed: %s",
                    export_trash_map(e, rmfid, rm_path), strerror(errno));
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
    }

    status = 0;
out:
    if (dd != NULL) {
        closedir(dd);
    }
    return status;
}

static void export_del_mfentry(export_t *e, mfentry_t *mfe) {
    DEBUG_FUNCTION;

    htable_del(&e->hfids, mfe->attrs.fid);
    htable_del(&e->h_pfids, mfe);
    list_remove(&mfe->list);
    e->csize--;
}

static void export_put_mfentry(export_t *e, mfentry_t *mfe) {
    DEBUG_FUNCTION;

    htable_put(&e->hfids, mfe->attrs.fid, mfe);
    htable_put(&e->h_pfids, mfe, mfe);
    list_push_front(&e->mfiles, &mfe->list);
    e->csize++;

    // if cache is full delete the tail of the list which should be the lru.
    // we only remove closed regular files.
    if (e->csize >= E_CSIZE) {
        mfentry_t *lru = list_entry(e->mfiles.prev, mfentry_t, list);
        while((lru != mfe) && (S_ISDIR(lru->attrs.mode) || (lru->cnt))) {
            lru = list_entry(lru->list.prev, mfentry_t, list);
        }
        // Do not remove the entry we just put in (for sure we need it !)
        if (lru != mfe) {
            export_del_mfentry(e, lru);
            mfentry_release(lru);
            free(lru);
        }
    }
}

static mfentry_t *export_get_mfentry_by_id(export_t *e, fid_t fid) {
    mfentry_t *mfe = 0;
    DEBUG_FUNCTION;

    if ((mfe = htable_get(&e->hfids, fid)) == 0) {
        errno = ESTALE;
    } else {
        // push the lru.
        list_remove(&mfe->list);
        list_push_front(&e->mfiles, &mfe->list);
    }
    return mfe;
}

static mfentry_t *export_get_mfentry_by_parent_name(export_t *e, mfentry_t *key) {
    mfentry_t *mfe = 0;
    DEBUG_FUNCTION;

    if ((mfe = htable_get(&e->h_pfids, key)) == 0) {
        errno = ESTALE;
    } else {
        // push the lru.
        list_remove(&mfe->list);
        list_push_front(&e->mfiles, &mfe->list);
    }
    return mfe;
}

static inline int export_update_files(export_t *e, int32_t n) {
    int status = -1;
    uint64_t files;
    DEBUG_FUNCTION;

    if (getxattr(e->root, EFILESKEY, &files, sizeof (uint64_t)) == -1) {
        warning("export_update_files failed: getxattr for file %s failed: %s",
                e->root, strerror(errno));
        goto out;
    }

    files += n;

    if (setxattr(e->root, EFILESKEY, &files, sizeof (uint64_t), XATTR_REPLACE)
            == -1) {
        warning("export_update_files failed: setxattr for file %s failed: %s",
                e->root, strerror(errno));
        goto out;
    }
    status = 0;

out:
    return status;
}

static inline int export_update_blocks(export_t * e, int32_t n) {
    int status = -1;
    uint64_t blocks;
    DEBUG_FUNCTION;

    if (getxattr(e->root, EBLOCKSKEY, &blocks, sizeof (uint64_t)) !=
            sizeof (uint64_t)) {
        warning("export_update_blocks failed: getxattr for file %s failed: %s",
                e->root, strerror(errno));
        goto out;
    }

    if (e->hquota > 0 && blocks + n > e->hquota) {
        warning("quota exceed: %"PRIu64" over %"PRIu64"", blocks + n, e->hquota);
        errno = EDQUOT;
        goto out;
    }

    blocks += n;

    if (setxattr(e->root, EBLOCKSKEY, &blocks, sizeof (uint64_t),
            XATTR_REPLACE) != 0) {
        warning("export_update_blocks failed: setxattr for file %s failed: %s",
                e->root, strerror(errno));
        goto out;
    }
    status = 0;

out:
    return status;
}

int export_create(const char *root) {
    int status = -1;
    const char *version = VERSION;
    char path[PATH_MAX];
    mattr_t attrs;
    uint64_t zero = 0;
    char trash_path[PATH_MAX + FILENAME_MAX + 1];
    uuid_t trash_uuid;
    char trash_str[37];
    DEBUG_FUNCTION;

    if (!realpath(root, path))
        goto out;
    if (export_check_root(path) != 0)
        goto out;
    if (setxattr(path, EBLOCKSKEY, &zero, sizeof (zero), XATTR_CREATE) != 0)
        goto out;
    if (setxattr(path, EFILESKEY, &zero, sizeof (zero), XATTR_CREATE) != 0)
        goto out;
    if (setxattr(path, EVERSIONKEY, &version,
            sizeof (char) * strlen(version) + 1, XATTR_CREATE) != 0)
        goto out;

    memset(&attrs, 0, sizeof (mattr_t));
    uuid_generate(attrs.fid);
    attrs.cid = 0;
    memset(attrs.sids, 0, ROZOFS_SAFE_MAX * sizeof (sid_t));
    attrs.mode =
            S_IFDIR | S_IRUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH |
            S_IWOTH | S_IXOTH;
    attrs.nlink = 2;
    attrs.uid = 0; // root
    attrs.gid = 0; // root
    if ((attrs.ctime = attrs.atime = attrs.mtime = time(NULL)) == -1)
        goto out;
    attrs.size = ROZOFS_DIR_SIZE;
    if (setxattr(path, EATTRSTKEY, &attrs, sizeof (mattr_t), XATTR_CREATE)
            != 0)
        goto out;

    uuid_generate(trash_uuid);
    uuid_unparse(trash_uuid, trash_str);
    strcpy(trash_path, root);
    strcat(trash_path, "/");
    strcat(trash_path, trash_str);

    if (mkdir(trash_path, S_IRWXU) != 0)
        goto out;

    if (setxattr(path, ETRASHUUID, trash_uuid, sizeof (uuid_t), XATTR_CREATE)
            != 0)
        goto out;

    status = 0;
out:

    return status;
}

int export_initialize(export_t * e, volume_t *volume, uint32_t eid,
        const char *root, const char *md5, uint64_t squota, uint64_t hquota) {
    int status = -1;
    mfentry_t *mfe;
    uuid_t trash_uuid;
    char trash_str[37];
    DEBUG_FUNCTION;

    if (!realpath(root, e->root))
        goto out;
    if (export_check_root(e->root) != 0)
        goto out;
    if (export_check_setup(e->root) != 0) {
        if (export_create(root) != 0) {
            goto out;
        }
    }

    e->eid = eid;
    e->volume = volume;

    if (strlen(md5) == 0) {
        memcpy(e->md5, ROZOFS_MD5_NONE, ROZOFS_MD5_SIZE);
    } else {
        memcpy(e->md5, md5, ROZOFS_MD5_SIZE);
    }

    e->squota = squota;
    e->hquota = hquota;

    list_init(&e->mfiles);
    list_init(&e->rmfiles);

    if ((errno = pthread_rwlock_init(&e->rm_lock, NULL)) != 0) {
        status = -1;
        goto out;
    }

    htable_initialize(&e->hfids, EHSIZE, mfentry_hash_fid, mfentry_cmp_fid);
    htable_initialize(&e->h_pfids, EHSIZE, mfentry_hash_fid_name,
            mfentry_cmp_fid_name);

    e->csize = 0;

    // Register the root
    mfe = xmalloc(sizeof (mfentry_t));
    if (mfentry_initialize(mfe, 0, e->root, e->root, 0) != 0)
        goto out;
    if (mfentry_read(mfe) != 0)
        goto out;

    export_put_mfentry(e, mfe);
    memcpy(e->rfid, mfe->attrs.fid, sizeof (fid_t));

    if (getxattr(root, ETRASHUUID, &(trash_uuid), sizeof (uuid_t)) == -1) {
        severe("export_initialize failed: getxattr for file %s failed: %s",
                root, strerror(errno));
        goto out;
    }

    uuid_unparse(trash_uuid, trash_str);
    strcpy(e->trashname, trash_str);

    if (export_load_rmfentry(e) != 0) {
        severe("export_initialize failed: export_load_rmfentry failed: %s",
                strerror(errno));
        goto out;
    }

    status = 0;
out:

    return status;
}

void export_release(export_t * e) {

    list_t *p, *q;
    DEBUG_FUNCTION;

    list_for_each_forward_safe(p, q, &e->mfiles) {

        mfentry_t *mfe = list_entry(p, mfentry_t, list);
        export_del_mfentry(e, mfe);
        mfentry_release(mfe);
        free(mfe);
    }

    list_for_each_forward_safe(p, q, &e->rmfiles) {

        rmfentry_t *rmfe = list_entry(p, rmfentry_t, list);
        list_remove(&rmfe->list);
        free(rmfe);
    }

    htable_release(&e->hfids);
    htable_release(&e->h_pfids);

    pthread_rwlock_destroy(&e->rm_lock);
}

int export_stat(export_t * e, estat_t * st) {
    int status = -1;
    struct statfs stfs;
    volume_stat_t vstat;
    DEBUG_FUNCTION;

    st->bsize = ROZOFS_BSIZE;
    if (statfs(e->root, &stfs) != 0)
        goto out;
    st->namemax = stfs.f_namelen;
    st->ffree = stfs.f_ffree;
    if (getxattr(e->root, EBLOCKSKEY, &(st->blocks), sizeof (uint64_t)) == -1)
        goto out;
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
    // blocks store in EBLOCKSKEY is the number of currently stored blocks
    // blocks in estat_t is the total number of blocks (see struct statvfs)
    // rozofs does not have a constant total number of blocks
    // it depends on usage made of storage (through other services)
    st->blocks += st->bfree;
    if (getxattr(e->root, EFILESKEY, &(st->files), sizeof (uint64_t)) == -1)
        goto out;

    status = 0;
out:

    return status;
}

int export_lookup(export_t *e, fid_t parent, const char *name,
        mattr_t *attrs) {
    int status = -1;
    char path[PATH_MAX + FILENAME_MAX + 1];
    mfentry_t *pmfe = 0;
    mfentry_t *mfe = 0;
    mfentry_t *mfkey = 0;
    DEBUG_FUNCTION;

    // Get the mfentry for the parent from htable hfids
    if (!(pmfe = export_get_mfentry_by_id(e, parent)))
        goto out;

    // Manage "." and ".."
    if (strcmp(name, ".") == 0) {
        memcpy(attrs, &pmfe->attrs, sizeof (mattr_t));
        status = 0;
        goto out;
    }
    if (strcmp(name, "..") == 0) {
        if (uuid_compare(parent, e->rfid) == 0) // We're looking for root parent
            memcpy(attrs, &pmfe->attrs, sizeof (mattr_t));
        else
            memcpy(attrs, &pmfe->parent->attrs, sizeof (mattr_t));
        status = 0;
        goto out;
    }

    // Manage trash directory
    // XXX We can make this check on client
    if ((uuid_compare(parent, e->rfid) == 0) && strcmp(name, e->trashname) == 0) {
        errno = ENOENT;
        goto out;
    }

    // Check if this file is already cached
    mfkey = xmalloc(sizeof (mfentry_t));
    memcpy(mfkey->pfid, pmfe->attrs.fid, sizeof (fid_t));
    mfkey->name = xstrdup(name);

    if (!(mfe = export_get_mfentry_by_parent_name(e, mfkey))) {
        // If no cached, test the existence of this file
        strcpy(path, pmfe->path);
        strcat(path, "/");
        strcat(path, name);
        if (access(path, F_OK) == 0) {
            // If the file exists we cache it
            mattr_t fake;
            mfe = xmalloc(sizeof (mfentry_t));
            if (mfentry_initialize(mfe, pmfe, name, path, &fake) != 0)
                goto error;
            if (mfentry_read(mfe) != 0)
                goto error;
            export_put_mfentry(e, mfe);
            /* We can return attributes from the hash table h_pfids
             *  because we read the data before
             * They are necessarily update even in the case of hardlinks*/
            memcpy(attrs, &mfe->attrs, sizeof (mattr_t));
            status = 0;
            goto out;
        } else {
            // If the file does not exist
            goto out;
        }
    } else { // This file is already cached
        if (!S_ISDIR(mfe->attrs.mode)) {
            /* We can not return attributes from the hash table h_pfids
             *  because they are not necessarily up to date
             * in the case of hardlinks*/
            mfentry_t *mfe_fid = 0;
            // Get the mfentry for this file from htable hfids
            if (!(mfe_fid = export_get_mfentry_by_id(e, mfe->attrs.fid)))
                goto out;
            memcpy(attrs, &mfe_fid->attrs, sizeof (mattr_t));
            status = 0;
            goto out;
        } else {
            memcpy(attrs, &mfe->attrs, sizeof (mattr_t));
            status = 0;
            goto out;
        }
    }
    if (!mfe) {
        warning("export_lookup failed but file: %s exists", name);
        errno = ENOENT;
    }

    goto out;
error:
    if (mfe)
        free(mfe);
out:
    if (mfkey) {
        if (mfkey->name)
            free(mfkey->name);
        free(mfkey);
    }
    return status;
}

int export_getattr(export_t * e, fid_t fid, mattr_t * attrs) {
    int status = -1;
    mfentry_t *mfe = 0;
    DEBUG_FUNCTION;

    if (!(mfe = export_get_mfentry_by_id(e, fid)))
        goto out;

    memcpy(attrs, &mfe->attrs, sizeof (mattr_t));
    status = 0;
out:

    return status;
}

int export_setattr(export_t * e, fid_t fid, mattr_t * attrs) {
    int status = -1;
    mfentry_t *mfe = 0;
    dist_t empty = 0;
    int fd = -1;
    DEBUG_FUNCTION;

    if (!(mfe = export_get_mfentry_by_id(e, fid)))
        goto out;

    // XXX IS IT POSSIBLE WITH A DIRECTORY?
    if (mfe->attrs.size != attrs->size) {

        if (attrs->size >= 0x20000000000LL) {
            errno = EFBIG;
            goto out;
        }

        uint64_t nrb_new = ((attrs->size + ROZOFS_BSIZE - 1) / ROZOFS_BSIZE);
        uint64_t nrb_old = ((mfe->attrs.size + ROZOFS_BSIZE - 1) /
                ROZOFS_BSIZE);

        // Open the file descriptor
        if ((fd = open(mfe->path, O_RDWR)) < 0) {
            severe("export_setattr failed: open for file %s failed: %s",
                    mfe->path, strerror(errno));
            goto out;
        }
        if (mfe->attrs.size > attrs->size) {
            if (ftruncate(fd, nrb_new * sizeof (dist_t)) != 0)
                goto out;
        } else {
            off_t count = 0;
            for (count = nrb_old; count < nrb_new; count++) {
                if (pwrite
                        (fd, &empty, sizeof (dist_t),
                        count * sizeof (dist_t)) != sizeof (dist_t)) {
                    severe("export_setattr: pwrite failed : %s",
                            strerror(errno));
                    goto out;
                }
            }
        }
        if (export_update_blocks(e, ((int32_t) nrb_new - (int32_t) nrb_old))
                != 0)
            goto out;

        mfe->attrs.size = attrs->size;
    }

    mfe->attrs.mode = attrs->mode;
    mfe->attrs.uid = attrs->uid;
    mfe->attrs.gid = attrs->gid;
    mfe->attrs.nlink = attrs->nlink;
    mfe->attrs.ctime = time(NULL);
    mfe->attrs.atime = attrs->atime;
    mfe->attrs.mtime = attrs->mtime;

    if (mfentry_write(mfe) != 0)
        goto out;

    status = 0;

out:
    if (fd != -1)
        close(fd);

    return status;
}

int export_readlink(export_t * e, uuid_t fid, char *link) {
    int status = -1;
    int xerrno = errno;
    mfentry_t *mfe;
    int fd = 0;
    DEBUG_FUNCTION;

    if (!(mfe = export_get_mfentry_by_id(e, fid)))
        goto out;

    if ((fd = open(mfe->path, O_RDONLY)) < 0)
        goto error;
    if (read(fd, link, sizeof(char) * ROZOFS_PATH_MAX) == -1)
        goto error;
    if (close(fd) != 0)
        goto error;
    status = 0;

error:
    xerrno = errno;
    if (fd >= 0)
        close(fd);
    errno = xerrno;

out:
    return status;
}

int export_link(export_t *e, fid_t inode, fid_t newparent, const char *newname, mattr_t *attrs) {
    int status = -1;
    mfentry_t *mfe = 0;
    mfentry_t *pmfe = 0;
    mfentry_t *nmfe = 0;
    char newpath[PATH_MAX + FILENAME_MAX + 1];
    int xerrno = errno;
    DEBUG_FUNCTION;

    // Get mfe for inode
    if (!(mfe = export_get_mfentry_by_id(e, inode)))
        goto out;

    // Get mfe for new_parent
    if (!(pmfe = export_get_mfentry_by_id(e, newparent)))
        goto out;

    // Make the hardlink
    strcpy(newpath, pmfe->path);
    strcat(newpath, "/");
    strcat(newpath, newname);
    if (link(mfe->path, newpath) != 0)
        goto out;

    // Update nlink and ctime for inode
    mfe->attrs.nlink++;
    mfe->attrs.ctime = time(NULL);

    // Effective write for the inode
    if (mfentry_write(mfe) != 0)
        goto error; // XXX Link is already made

    // Update new parent
    pmfe->attrs.mtime = pmfe->attrs.ctime = time(NULL);

    // Effective write for parent
    if (mfentry_write(pmfe) != 0)
        goto error; // XXX Link is already made

    // Create new mfe for this new link
    // We get attrs from htable hfids
    nmfe = xmalloc(sizeof (mfentry_t));
    if (mfentry_initialize(nmfe, pmfe, newname, newpath, &mfe->attrs) != 0)
        goto error; // XXX Link is already made

    // Put this mfentry on :
    // htable &e->pfids
    // list &e->mfiles
    htable_put(&e->h_pfids, nmfe, nmfe);
    list_push_front(&e->mfiles, &nmfe->list);

    // Not necessary to do a effective write for this inode (already up to date)

    // Put the mattrs for the response
    memcpy(attrs, &mfe->attrs, sizeof (mattr_t));

    status = 0;
    goto out;
error:
    xerrno = errno;
    if (mfe) {
        mfentry_release(mfe);
        free(mfe);
    }
    errno = xerrno;
out:
    return status;
}

int export_mknod(export_t *e, uuid_t parent, const char *name, uint32_t uid,
        uint32_t gid, mode_t mode, mattr_t *attrs) {
    int status = -1;
    char path[PATH_MAX + FILENAME_MAX + 1];
    mfentry_t *pmfe = 0;
    mfentry_t *mfe = 0;
    int xerrno = errno;
    DEBUG_FUNCTION;

    if (!(pmfe = export_get_mfentry_by_id(e, parent)))
        goto out;

    strcpy(path, pmfe->path);
    strcat(path, "/");
    strcat(path, name);
    // XXX we could use an other mode
    if (mknod(path, mode, 0) != 0)
        goto out;

    uuid_generate(attrs->fid);
    /* Get a distribution of one cluster included in the volume given by the export */
    if (volume_distribute(e->volume, &attrs->cid, attrs->sids) != 0)
        goto error;
    attrs->mode = mode;
    attrs->uid = uid;
    attrs->gid = gid;
    attrs->nlink = 1;
    if ((attrs->ctime = attrs->atime = attrs->mtime = time(NULL)) == -1)
        goto error;
    attrs->size = 0;

    mfe = xmalloc(sizeof (mfentry_t));
    if (mfentry_initialize(mfe, pmfe, name, path, attrs) != 0)
        goto error;
    if (mfentry_create(mfe) != 0)
        goto error;

    pmfe->attrs.mtime = pmfe->attrs.ctime = time(NULL);
    if (mfentry_write(pmfe) != 0)
        goto error;

    if (export_update_files(e, 1) != 0)
        goto error;

    export_put_mfentry(e, mfe);

    status = 0;
    goto out;
error:
    xerrno = errno;
    if (mfe) {
        mfentry_release(mfe);
        free(mfe);
    }
    if (xerrno != EEXIST) {
        unlink(path);
    }
    errno = xerrno;
out:

    return status;
}

int export_mkdir(export_t * e, uuid_t parent, const char *name, uint32_t uid,
        uint32_t gid, mode_t mode, mattr_t * attrs) {
    int status = -1;
    char path[PATH_MAX + FILENAME_MAX + 1];
    mfentry_t *pmfe = 0;
    mfentry_t *mfe = 0;
    int xerrno;
    DEBUG_FUNCTION;

    if (!(pmfe = export_get_mfentry_by_id(e, parent)))
        goto out;

    strcpy(path, pmfe->path);
    strcat(path, "/");
    strcat(path, name);
    if (mkdir(path, mode) != 0)
        goto error;

    uuid_generate(attrs->fid);
    attrs->cid = 0;
    memset(attrs->sids, 0, ROZOFS_SAFE_MAX * sizeof (uint16_t));
    attrs->mode = mode;
    attrs->uid = uid;
    attrs->gid = gid;
    attrs->nlink = 2;
    if ((attrs->ctime = attrs->atime = attrs->mtime = time(NULL)) == -1)
        goto error;
    attrs->size = ROZOFS_BSIZE;

    mfe = xmalloc(sizeof (mfentry_t));
    if (mfentry_initialize(mfe, pmfe, name, path, attrs) != 0)
        goto error;
    if (mfentry_create(mfe) != 0)
        goto error;

    pmfe->attrs.nlink++;
    pmfe->attrs.mtime = pmfe->attrs.ctime = time(NULL);

    if (mfentry_write(pmfe) != 0) {
        pmfe->attrs.nlink--;
        goto error;
    }

    if (export_update_files(e, 1) != 0)
        goto error;

    export_put_mfentry(e, mfe);

    status = 0;
    goto out;
error:
    xerrno = errno;
    if (mfe) {
        mfentry_release(mfe);
        free(mfe);
    }
    if (xerrno != EEXIST) {
        rmdir(path);
    }
    errno = xerrno;
out:

    return status;
}

static int unlink_hard_link(export_t * e, mfentry_t * mfe_fid, mfentry_t * mfe_pfid_name, fid_t * fid, uint8_t rm_file) {
    int status = -1;
    DEBUG_FUNCTION;

    // If rm_file == 1 you need to remove the file
    if (rm_file == 1) {
        // Remove file
        if (unlink(mfe_pfid_name->path) == -1)
            goto out;
    }

    // Update mtime and ctime for parent directory
    if (mfe_pfid_name->parent != NULL) {
        mfe_pfid_name->parent->attrs.mtime = mfe_pfid_name->parent->attrs.ctime = time(NULL);
        if (mfentry_write(mfe_pfid_name->parent) != 0)
            goto out; // XXX The inode is already deleted
    }

    // Verify if mfentry is the same on the 2 htables
    if (strcmp(mfe_pfid_name->path, mfe_fid->path) == 0) {

        // This mfentry is the same on h_pfids and on h_fids
        // Delete mfentry from htable h_pfids
        htable_del(&e->h_pfids, mfe_pfid_name);
        // Delete mfentry from htable hfids
        htable_del(&e->hfids, mfe_fid->attrs.fid);

        // Pay attention now this entry is no longer up to date
        // parent, pfid, path, name, fd, cnt are not up to date

        // Search in list another entry with the same fid
        list_t *p;

        list_for_each_forward(p, &e->mfiles) {
            mfentry_t *mfentry = list_entry(p, mfentry_t, list);
            if ((uuid_compare(mfentry->attrs.fid, mfe_fid->attrs.fid) == 0) && (strcmp(mfentry->path, mfe_fid->path) != 0)) {
                // Updates attributes with the only mattr we know who is up to date
                memcpy(&mfentry->attrs, &mfe_fid->attrs, sizeof (mattr_t));
                // Update nlink for mfentry h_fids 
                mfentry->attrs.nlink--;
                // Add this mfentry on the htable hfids
                htable_put(&e->hfids, mfentry->attrs.fid, mfentry);
                // Effective write
                if (mfentry_write(mfentry) != 0)
                    goto out; // XXX The inode is already deleted
                break;
            }
        }
        // Remove mfentry from mfiles list for this export
        list_remove(&mfe_fid->list);
        // Free memory for this mfentry  
        mfentry_release(mfe_fid);
        free(mfe_fid);

    } else { // This mfentry is not the same on h_pfids and on h_fids

        // Delete entry on h_pfids
        htable_del(&e->h_pfids, mfe_pfid_name);
        // Remove entry from mfiles list for this export
        list_remove(&mfe_pfid_name->list);
        // Free memory for this mfentry  
        mfentry_release(mfe_pfid_name); // What for file descriptor ?
        free(mfe_pfid_name);
        // Update nlink for mfentry h_fids 
        mfe_fid->attrs.nlink--;
        // Effective write
        if (mfentry_write(mfe_fid) != 0)
            goto out; // XXX The inode is already deleted
    }
    // Return a empty fid because no inode has been deleted
    memset(fid, 0, 16);

    status = 0;
out:
    return status;
}

static int unlink_file(export_t * e, mfentry_t * mfe_fid, fid_t * fid, uint8_t mv_file) {
    int status = -1;
    char file_path[PATH_MAX + NAME_MAX + 1];
    char rm_path[PATH_MAX + NAME_MAX + 1];
    rmfentry_t *rmfe = 0;
    uint64_t size = 0;
    mode_t mode;
    DEBUG_FUNCTION;

    // If the file is empty is not neccessary to delete bins on each storages
    if (mfe_fid->attrs.size > 0) {

        // If mv_file == 1 move the physical file to the trash directory
        if (mv_file == 1) {
            strcpy(file_path, mfe_fid->path);
            if (rename(file_path, export_trash_map(e, mfe_fid->attrs.fid, rm_path)) == -1) {
                severe("unlink_file failed: rename(%s,%s) failed: %s", file_path, rm_path, strerror(errno));
                goto out;
            }
        }

        // Preparation of the rmfentry
        rmfe = xmalloc(sizeof (rmfentry_t));
        memcpy(rmfe->fid, mfe_fid->attrs.fid, sizeof (fid_t));
        memcpy(rmfe->sids, mfe_fid->attrs.sids, sizeof (sid_t) * ROZOFS_SAFE_MAX);
        list_init(&rmfe->list);

        // Adding the rmfentry to the list of files to delete
        if ((errno = pthread_rwlock_wrlock(&e->rm_lock)) != 0)
            goto out; // XXX The inode is already renamed
        list_push_back(&e->rmfiles, &rmfe->list);
        if ((errno = pthread_rwlock_unlock(&e->rm_lock)) != 0)
            goto out; // XXX The inode is already renamed
    } else {
        // Remove file
        if (mv_file == 1) {
            if (unlink(mfe_fid->path) == -1)
                goto out;
        }
    }

    // Update mtime and ctime for parent directory
    if (mfe_fid->parent != NULL) {
        mfe_fid->parent->attrs.mtime = mfe_fid->parent->attrs.ctime = time(NULL);
        if (mfentry_write(mfe_fid->parent) != 0)
            goto out; // XXX The inode is already renamed
    }

    // Return the removed fid
    memcpy(fid, mfe_fid->attrs.fid, sizeof (fid_t));

    // Removed mfentry
    export_del_mfentry(e, mfe_fid);
    mfentry_release(mfe_fid);
    size = mfe_fid->attrs.size;
    mode = mfe_fid->attrs.mode;
    free(mfe_fid);

    // Update the nb. of files for this export
    if (export_update_files(e, -1) != 0)
        goto out;

    // Update the nb. of blocks for this export
    if (!S_ISLNK(mode))
        if (export_update_blocks(e, -(((int64_t) size + ROZOFS_BSIZE - 1) / ROZOFS_BSIZE)) != 0)
            goto out;

    status = 0;
out:
    return status;
}

int export_unlink(export_t * e, fid_t pfid, const char *name, fid_t * fid) {
    int status = -1;
    mfentry_t *mfe_pfid_name = 0;
    mfentry_t *mfe_fid = 0;
    mfentry_t *mfkey = 0;
    uint16_t nlink = 0;
    DEBUG_FUNCTION;

    // Get mfentry from htable h_pfids
    mfkey = xmalloc(sizeof (mfentry_t));
    memcpy(mfkey->pfid, pfid, sizeof (fid_t));
    mfkey->name = xstrdup(name);
    if (!(mfe_pfid_name = export_get_mfentry_by_parent_name(e, mfkey)))
        goto out;

    // Get mfentry from htable h_fids
    if (!(mfe_fid = export_get_mfentry_by_id(e, mfe_pfid_name->attrs.fid)))
        goto out;

    // Get the nb. of nlink for this file
    nlink = mfe_fid->attrs.nlink;

    // If nlink = 1, it's a normal file
    if (nlink == 1) {
        if (unlink_file(e, mfe_fid, fid, 1) != 0) {
            goto out;
        }
    }

    // If nlink > 1, it's a hardlink
    if (nlink > 1) {
        if (unlink_hard_link(e, mfe_fid, mfe_pfid_name, fid, 1) != 0) {
            goto out;
        }
    }

    status = 0;
out:
    if (mfkey) {
        if (mfkey->name)
            free(mfkey->name);
        free(mfkey);
    }
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

            storageclt_t * sclt = (storageclt_t *) xmalloc(sizeof (storageclt_t));

            strcpy(sclt->host, vs->host);
            sclt->sid = vs->sid;

            if (storageclt_initialize(sclt) != 0) {
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

static storageclt_t * lookup_cnx(list_t *list, sid_t sid) {
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
        storageclt_release(cnx_entry->cnx);
        list_remove(p);
        free(cnx_entry);
    }
}

int export_rm_bins(export_t * e) {
    int status = -1;
    int cnt = 0;
    int release = 0;
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
    while (!list_empty(&e->rmfiles)) {

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
                storageclt_t* stor = lookup_cnx(&connexions, *it);

                // Send request to storage for remove bins
                if (storageclt_remove(stor, entry->fid) != 0) {
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
            char path[PATH_MAX + NAME_MAX + 1];
            // In case of rename it's not neccessary to remove trash file
            if (unlink(export_trash_map(e, entry->fid, path)) == -1) {
                if (errno != ENOENT) {
                    severe("unlink failed: unlink file %s failed: %s", export_trash_map(e, entry->fid, path), strerror(errno));
                    goto out;
                }
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
    }

    if (release == 1) {
        // Release storage connexions
        release_storages_cnx(&connexions);
    }

    status = 0;
out:
    return status;
}

int export_rmdir(export_t * e, fid_t pfid, const char *name, fid_t * fid) {
    int status = -1;
    mfentry_t *mfkey = 0;
    mfentry_t *mfe = 0;
    DEBUG_FUNCTION;

    // Get the mfentry for the directory to remove from htable h_pfids
    mfkey = xmalloc(sizeof (mfentry_t));
    memcpy(mfkey->pfid, pfid, sizeof (fid_t));
    mfkey->name = xstrdup(name);
    if (!(mfe = export_get_mfentry_by_parent_name(e, mfkey)))
        goto out;

    // We return the fid of removed directory
    memcpy(fid, mfe->attrs.fid, sizeof (fid_t));

    // Remove directory
    if (rmdir(mfe->path) == -1)
        goto out;

    // Update the nb. of files for this export
    if (export_update_files(e, -1) != 0)
        goto out; // XXX PROBLEM: THE DIRECTORY IS REMOVED

    // Update the nlink and times of parent
    if (mfe->parent != NULL) {
        mfe->parent->attrs.nlink--;
        mfe->parent->attrs.mtime = mfe->parent->attrs.ctime = time(NULL);
        if (mfentry_write(mfe->parent) != 0) {
            mfe->parent->attrs.nlink++;
            goto out; // XXX PROBLEM: THE DIRECTORY IS REMOVED
        }
    }

    // Removed mfentry
    export_del_mfentry(e, mfe);
    mfentry_release(mfe);
    free(mfe);

    status = 0;
out:
    if (mfkey) {
        if (mfkey->name)
            free(mfkey->name);
        free(mfkey);
    }
    return status;
}

/*
   symlink creates a regular file puts right mattrs in xattr 
   and the link path in file.
 */
int export_symlink(export_t * e, const char *link, uuid_t parent,
        const char *name, mattr_t * attrs) {
    int status = -1;
    char path[PATH_MAX + NAME_MAX + 1];
    char lname[ROZOFS_PATH_MAX];
    mfentry_t *pmfe = 0;
    mfentry_t *mfe = 0;
    int fd = -1;
    int xerrno = errno;
    DEBUG_FUNCTION;

    if (!(pmfe = export_get_mfentry_by_id(e, parent)))
        goto out;

    // make the link
    strcpy(path, pmfe->path);
    strcat(path, "/");
    strcat(path, name);
    if (mknod(path, S_IFREG|S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP|
                S_IROTH|S_IWOTH|S_IXOTH, 0) != 0)
        goto out;

    uuid_generate(attrs->fid);
    attrs->cid = 0;
    memset(attrs->sids, 0, ROZOFS_SAFE_MAX * sizeof (uint16_t));
    attrs->mode = S_IFLNK|S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|
        S_IXGRP|S_IROTH|S_IWOTH|S_IXOTH;
    attrs->uid = geteuid();
    attrs->gid = getegid();
    attrs->nlink = 1;
    if ((attrs->ctime = attrs->atime = attrs->mtime = time(NULL)) == -1)
        goto error;
    attrs->size = ROZOFS_BSIZE;

    // write the link name
    if ((fd = open(path, O_RDWR)) < 0)
        goto error;
    strcpy(lname, link);
    if (write(fd, lname, sizeof(char) * ROZOFS_PATH_MAX) == -1)
        goto error;
    if (close(fd) != 0)
        goto error;

    mfe = xmalloc(sizeof (mfentry_t));
    if (mfentry_initialize(mfe, pmfe, name, path, attrs) != 0)
        goto error;
    if (mfentry_create(mfe) != 0)
        goto error;

    pmfe->attrs.mtime = pmfe->attrs.ctime = time(NULL);
    pmfe->attrs.nlink++;
    if (mfentry_write(pmfe) != 0) {
        pmfe->attrs.nlink--;
        goto error;
    }

    if (export_update_files(e, 1) != 0)
        goto error;

    export_put_mfentry(e, mfe);

    status = 0;
    goto out;
error:
    xerrno = errno;
    if (fd >= 0)
        close(fd);
    if (mfe) {
        mfentry_release(mfe);
        free(mfe);
    }
    if (xerrno != EEXIST) {
        unlink(path);
    }
out:

    return status;
}

int export_rename(export_t * e, fid_t pfid, const char *name, fid_t npfid, const char *newname, fid_t * fid) {
    int status = -1;
    mfentry_t *fmfe_pfid_name = 0;
    mfentry_t *fmfe_fid = 0;
    mfentry_t *new_pmfe = 0;
    mfentry_t *old_mfe_pfid_name = 0;
    mfentry_t *mfkey = 0;
    mfentry_t *to_mfkey = 0;
    char new_path[PATH_MAX + NAME_MAX + 1];
    DEBUG_FUNCTION;

    // Get the mfentry for the file to rename from htable h_pfids
    mfkey = xmalloc(sizeof (mfentry_t));
    memcpy(mfkey->pfid, pfid, sizeof (fid_t));
    mfkey->name = xstrdup(name);
    if (!(fmfe_pfid_name = export_get_mfentry_by_parent_name(e, mfkey)))
        goto out;

    // Get the mfentry for the file to rename from htable hfids
    if (!(fmfe_fid = export_get_mfentry_by_id(e, fmfe_pfid_name->attrs.fid)))
        goto out;

    // Get the mfentry for the new parent
    if (!(new_pmfe = export_get_mfentry_by_id(e, npfid)))
        goto out;

    // Rename file
    strcpy(new_path, new_pmfe->path);
    strcat(new_path, "/");
    strcat(new_path, newname);

    if (rename(fmfe_pfid_name->path, new_path) == -1)
        goto out;

    // We return the fid of the file being replaced (crushed)
    // by default we puts the identifier 0 (no file replaced)
    memset(fid, 0, 16);

    // See if the target file or directory already existed
    to_mfkey = xmalloc(sizeof (mfentry_t));
    memcpy(to_mfkey->pfid, new_pmfe->attrs.fid, sizeof (fid_t));
    to_mfkey->name = xstrdup(newname);
    if ((old_mfe_pfid_name = export_get_mfentry_by_parent_name(e, to_mfkey))) {

        // Get mfentry from htable h_fids
        mfentry_t *old_mfe_fid = 0;
        if (!(old_mfe_fid = export_get_mfentry_by_id(e, old_mfe_pfid_name->attrs.fid)))
            goto out;

        // If old_mfe is a directory
        if (S_ISDIR(old_mfe_fid->attrs.mode)) {
            // Update the nb. of blocks for this export
            if (export_update_files(e, -1) != 0)
                goto out;
            // Update the nlink and times of parent
            new_pmfe->attrs.nlink--;
            new_pmfe->attrs.mtime = new_pmfe->attrs.ctime = time(NULL);
            if (mfentry_write(new_pmfe) != 0) {
                new_pmfe->attrs.nlink++;
                goto out;
            }
            // Delete the old mfentry
            export_del_mfentry(e, old_mfe_fid);
            mfentry_release(old_mfe_fid);
            free(old_mfe_fid);

            // Return the removed fid
            memcpy(fid, old_mfe_fid->attrs.fid, sizeof (fid_t));
        }

        // If old_mfe is a symlink or a regular file
        if (S_ISREG(old_mfe_fid->attrs.mode) || S_ISLNK(old_mfe_fid->attrs.mode)) {

            uint16_t nlink_old_file = old_mfe_fid->attrs.nlink;

            // If nlink=1
            // XXX The physical file is not move to the trash directory
            if (nlink_old_file == 1)
                if (unlink_file(e, old_mfe_pfid_name, fid, 0) != 0)
                    goto out;

            // If nlink>1 (hardlink)
            if (nlink_old_file > 1) {
                if (unlink_hard_link(e, old_mfe_fid, old_mfe_pfid_name, fid, 0) != 0)
                    goto out;
            }
        }
    }
    // If the renamed file is a directory
    if (S_ISDIR(fmfe_fid->attrs.mode)) {
        // Update the nlink of new parent
        new_pmfe->attrs.nlink++;
        if (mfentry_write(new_pmfe) != 0) {
            new_pmfe->attrs.nlink--;
            goto out;
        }
        // Update the nlink of old parent
        fmfe_pfid_name->parent->attrs.nlink--;
        if (mfentry_write(fmfe_pfid_name->parent) != 0) {
            fmfe_pfid_name->parent->attrs.nlink++;
            goto out;
        }
    }

    /* Update mfentry for the renamed file*/
    htable_del(&e->h_pfids, fmfe_pfid_name);
    free(fmfe_pfid_name->path);
    fmfe_pfid_name->path = xstrdup(new_path);
    free(fmfe_pfid_name->name);
    fmfe_pfid_name->name = xstrdup(newname);
    fmfe_fid->attrs.ctime = time(NULL);
    fmfe_pfid_name->parent = new_pmfe;
    memcpy(fmfe_pfid_name->pfid, new_pmfe->attrs.fid, sizeof (fid_t));
    htable_put(&e->h_pfids, fmfe_pfid_name, fmfe_pfid_name);

    /* Writing physical mattr in the file.*/
    if (mfentry_write(fmfe_fid) != 0)
        goto out;

    status = 0;
out:
    if (to_mfkey) {
        if (to_mfkey->name)
            free(to_mfkey->name);
        free(to_mfkey);
    }
    if (mfkey) {
        if (mfkey->name)
            free(mfkey->name);
        free(mfkey);
    }
    return status;
}

int64_t export_read(export_t * e, uuid_t fid, uint64_t off, uint32_t len) {
    int64_t read = -1;
    mfentry_t *mfe = 0;
    DEBUG_FUNCTION;

    if (!(mfe = export_get_mfentry_by_id(e, fid)))
        goto out;

    if (off > mfe->attrs.size) {
        errno = 0;
        goto out;
    }

    if ((mfe->attrs.atime = time(NULL)) == -1)
        goto out;

    if (fsetxattr(mfe->fd, EATTRSTKEY, &mfe->attrs, sizeof (mattr_t),
            XATTR_REPLACE) != 0) {
        severe("export_read failed: fsetxattr in file %s failed: %s",
                mfe->path, strerror(errno));
        goto out;
    }
    read = off + len < mfe->attrs.size ? len : mfe->attrs.size - off;
out:

    return read;
}

int export_read_block(export_t * e, uuid_t fid, uint64_t bid, uint32_t n,
        dist_t * d) {
    int status = -1;
    mfentry_t *mfe = 0;
    DEBUG_FUNCTION;
    int nrb = 0;

    if (!(mfe = export_get_mfentry_by_id(e, fid)))
        goto out;

    if ((nrb =pread(mfe->fd, d, n * sizeof (dist_t), bid * sizeof (dist_t)))
            != n * sizeof (dist_t)) {
        severe("export_read_block failed: (bid: %"PRIu64", n: %u) pread in file %s failed: %s just %d bytes for %d blocks ",
                bid, n, mfe->path, strerror(errno), nrb, n);
        goto out;
    }

    status = 0;
out:

    return status;
}

int64_t export_write(export_t * e, uuid_t fid, uint64_t off, uint32_t len) {
    int64_t written = -1;
    mfentry_t *mfe = 0;
    DEBUG_FUNCTION;

    if (!(mfe = export_get_mfentry_by_id(e, fid)))
        goto out;

    if (off + len > mfe->attrs.size) {
        /* don't skip intermediate computation to keep ceil rounded */
        uint64_t nbold = (mfe->attrs.size + ROZOFS_BSIZE - 1) / ROZOFS_BSIZE;
        uint64_t nbnew = (off + len + ROZOFS_BSIZE - 1) / ROZOFS_BSIZE;

        if (export_update_blocks (e,  nbnew - nbold) != 0)
            goto out;

        mfe->attrs.size = off + len;
    }

    mfe->attrs.mtime = mfe->attrs.ctime = time(NULL);

    if (fsetxattr(mfe->fd, EATTRSTKEY, &mfe->attrs, sizeof (mattr_t),
            XATTR_REPLACE) != 0) {
        severe("export_write failed: fsetxattr in file %s failed: %s",
                mfe->path, strerror(errno));
        goto out;
    }

    written = len;

out:
    return written;
}

int export_write_block(export_t * e, uuid_t fid, uint64_t bid, uint32_t n,
        dist_t d) {
    int status = -1;
    mfentry_t *mfe = 0;
    off_t count;
    DEBUG_FUNCTION;

    if (!(mfe = export_get_mfentry_by_id(e, fid)))
        goto out;

    for (count = 0; count < n; count++) {
        if (pwrite(mfe->fd, &d, 1 * sizeof (dist_t),
                ((off_t) bid + count) * (off_t) sizeof (dist_t)) !=
                1 * sizeof (dist_t)) {
            severe("export_write_block failed: pwrite in file %s failed: %s",
                    mfe->path, strerror(errno));
            goto out;
        }
    }
    status = 0;
out:

    return status;
}

int export_readdir(export_t * e, fid_t fid, uint64_t cookie,
        child_t ** children, uint8_t * eof) {
    int status = -1, i;
    mfentry_t *mfe = NULL;
    DIR *dp;
    struct dirent *ep;
    child_t **iterator;
    int export_root = 0;

    DEBUG_FUNCTION;

    if (!(mfe = export_get_mfentry_by_id(e, fid)))
        goto out;

    // Open directory
    if (!(dp = opendir(mfe->path)))
        goto out;

    // Readdir first time
    ep = readdir(dp);

    // See if fid is the root directory
    if (uuid_compare(fid, e->rfid) == 0)
        export_root = 1;

    // Go to cookie index in this dir
    for (i = 0; i < cookie; i++) {
        if (ep) {
            ep = readdir(dp);
            // Check if the current directory is the trash
            if (export_root && strcmp(ep->d_name, e->trashname) == 0)
                i--;
        }
    }

    iterator = children;
    i = 0;

    // Readdir the next entries
    while (ep && i < MAX_DIR_ENTRIES) {
        mattr_t attrs;
        if (export_lookup(e, fid, ep->d_name, &attrs) == 0) {
            // Copy fid
            *iterator = xmalloc(sizeof (child_t)); // XXX FREE?
            memcpy((*iterator)->fid, &attrs.fid, sizeof (fid_t));
        } else {
            // Readdir for next entry
            ep = readdir(dp);
            continue;
        }

        // Copy name
        (*iterator)->name = xstrdup(ep->d_name); // XXX FREE?

        // Go to next entry
        iterator = &(*iterator)->next;

        // Readdir for next entry
        ep = readdir(dp);

        i++;
    }

    if (closedir(dp) == -1)
        goto out;

    if (ep)
        *eof = 0;
    else
        *eof = 1;

    mfe->attrs.atime = time(NULL);

    if (mfentry_write(mfe) != 0)
        goto out;

    *iterator = NULL;
    status = 0;
out:
    return status;
}

int export_open(export_t * e, fid_t fid) {
    int status = -1;
    mfentry_t *mfe = 0;
    int flag;
    DEBUG_FUNCTION;

    flag = O_RDWR;

    if (!(mfe = export_get_mfentry_by_id(e, fid)))
        goto out;

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

    if (!(mfe = export_get_mfentry_by_id(e, fid)))
        goto out;

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
