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

#define FUSE_USE_VERSION 26

#include <fuse/fuse_lowlevel.h>
#include <fuse/fuse_opt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>

#include "rozofs.h"
#include "config.h"
#include "list.h"
#include "log.h"
#include "file.h"
#include "htable.h"
#include "xmalloc.h"
#include "sproto.h"
#include <assert.h>

#define hash_xor8(n)    (((n) ^ ((n)>>8) ^ ((n)>>16) ^ ((n)>>24)) & 0xff)
#define INODE_HSIZE 8192
#define PATH_HSIZE  8192

#define FUSE28_DEFAULT_OPTIONS "default_permissions,allow_other,fsname=rozofs,subtype=rozofs,big_writes"
#define FUSE27_DEFAULT_OPTIONS "default_permissions,allow_other,fsname=rozofs,subtype=rozofs"

#define CACHE_TIMEOUT 10.0

static void usage(const char *progname) {
    fprintf(stderr, "Rozofs fuse mounter - %s\n", VERSION);
    fprintf(stderr, "Usage: %s mountpoint [options]\n", progname);
    fprintf(stderr, "general options:\n");
    fprintf(stderr, "\t-o opt,[opt...]\tmount options\n");
    fprintf(stderr, "\t-h --help\tprint help\n");
    fprintf(stderr, "\t-V --version\tprint rozofs version\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "ROZOFS options:\n");
    fprintf(stderr, "\t-H EXPORT_HOST\t\tdefine address (or dns name) where exportd deamon is running (default: rozofsexport) equivalent to '-o exporthost=EXPORT_HOST'\n");
    fprintf(stderr, "\t-E EXPORT_PATH\t\tdefine path of an export see exportd (default: /srv/rozo/exports/export) equivalent to '-o exportpath=EXPORT_PATH'\n");
    fprintf(stderr, "\t-P EXPORT_PASSWD\t\tdefine passwd used for an export see exportd (default: none) equivalent to '-o exportpasswd=EXPORT_PASSWD'\n");
    fprintf(stderr, "\t-o rozofsbufsize=N\tdefine size of I/O buffer in KiB (default: 256)\n");
    fprintf(stderr, "\t-o rozofsmaxretry=N\tdefine number of retries before I/O error is returned (default: 5)\n");
}

typedef struct rozofsmnt_conf {
    char *host;
    char *export;
    char *passwd;
    unsigned buf_size;
    unsigned max_retry;
} rozofsmnt_conf_t;

static rozofsmnt_conf_t conf;

static double direntry_cache_timeo = CACHE_TIMEOUT;
static double entry_cache_timeo = CACHE_TIMEOUT;
static double attr_cache_timeo = CACHE_TIMEOUT;

enum {
    KEY_EXPORT_HOST,
    KEY_EXPORT_PATH,
    KEY_EXPORT_PASSWD,
    KEY_HELP,
    KEY_VERSION,
};

#define MYFS_OPT(t, p, v) { t, offsetof(struct rozofsmnt_conf, p), v }

static struct fuse_opt rozofs_opts[] = {
    MYFS_OPT("exporthost=%s", host, 0),
    MYFS_OPT("exportpath=%s", export, 0),
    MYFS_OPT("exportpasswd=%s", passwd, 0),
    MYFS_OPT("rozofsbufsize=%u", buf_size, 0),
    MYFS_OPT("rozofsmaxretry=%u", max_retry, 0),

    FUSE_OPT_KEY("-H ", KEY_EXPORT_HOST),
    FUSE_OPT_KEY("-E ", KEY_EXPORT_PATH),
    FUSE_OPT_KEY("-P ", KEY_EXPORT_PASSWD),

    FUSE_OPT_KEY("-V", KEY_VERSION),
    FUSE_OPT_KEY("--version", KEY_VERSION),
    FUSE_OPT_KEY("-h", KEY_HELP),
    FUSE_OPT_KEY("--help", KEY_HELP),
    FUSE_OPT_END
};

static int myfs_opt_proc(void *data, const char *arg, int key,
        struct fuse_args *outargs) {
    (void) data;
    switch (key) {
        case FUSE_OPT_KEY_OPT:
            return 1;
        case FUSE_OPT_KEY_NONOPT:
            return 1;
        case KEY_EXPORT_HOST:
            if (conf.host == NULL) {
                conf.host = strdup(arg + 2);
            }
            return 0;
        case KEY_EXPORT_PATH:
            if (conf.export == NULL) {
                conf.export = strdup(arg + 2);
            }
            return 0;
        case KEY_EXPORT_PASSWD:
            if (conf.passwd == NULL) {
                conf.passwd = strdup(arg + 2);
            }
            return 0;
        case KEY_HELP:
            usage(outargs->argv[0]);
            fuse_opt_add_arg(outargs, "-h"); // PRINT FUSE HELP
            fuse_parse_cmdline(outargs, NULL, NULL, NULL);
            fuse_mount(NULL, outargs);
            exit(1);
        case KEY_VERSION:
            fprintf(stderr, "rozofs version %s\n", VERSION);
            fuse_opt_add_arg(outargs, "--version"); // PRINT FUSE VERSION
            fuse_parse_cmdline(outargs, NULL, NULL, NULL);
            exit(0);
    }
    return 1;
}

/*
typedef struct dirbuf {
    char *p;
    size_t size;
} dirbuf_t;
 */

typedef struct dirbuf {
    char *p;
    size_t size;
    uint8_t eof;
    uint64_t cookie;
} dirbuf_t;

/** entry kept locally to map fuse_inode_t with rozofs fid_t */
typedef struct ientry {
    fuse_ino_t inode;       ///< value of the inode allocated by rozofs
    fid_t fid;              ///< unique file identifier associated with the file or directory
    dirbuf_t db;            ///< buffer used for directory listing
    unsigned long nlookup;   ///< number of lookup done on this entry (used for forget)
    list_t list;
} ientry_t;

static exportclt_t exportclt;

static list_t inode_entries;
static htable_t htable_inode;
static htable_t htable_fid;

static uint32_t fuse_ino_hash(void *n) {
    return hash_xor8(*(uint32_t *) n);
}

static int fuse_ino_cmp(void *v1, void *v2) {
    return (*(fuse_ino_t *) v1 - *(fuse_ino_t *) v2);
}

static int fid_cmp(void *key1, void *key2) {
    return memcmp(key1, key2, sizeof(fid_t));
}

static unsigned int fid_hash(void *key) {
    uint32_t hash = 0;
    uint8_t *c;
    for (c = key; c != key + 16; c++)
        hash = *c + (hash << 6) + (hash << 16) - hash;
    return hash;
}

static void ientries_release() {
    list_t *p, *q;

    htable_release(&htable_inode);
    htable_release(&htable_fid);

    list_for_each_forward_safe(p, q, &inode_entries) {
        ientry_t *entry = list_entry(p, ientry_t, list);
        list_remove(p);
        free(entry);
    }
}

static void put_ientry(ientry_t * ie) {
    DEBUG("put inode: %lu\n", ie->inode);
    htable_put(&htable_inode, &ie->inode, ie);
    htable_put(&htable_fid, ie->fid, ie);
    list_push_front(&inode_entries, &ie->list);
}

static void del_ientry(ientry_t * ie) {
    DEBUG("del inode: %lu\n", ie->inode);
    htable_del(&htable_inode, &ie->inode);
    htable_del(&htable_fid, ie->fid);
    list_remove(&ie->list);
}

static ientry_t *get_ientry_by_inode(fuse_ino_t ino) {
    return htable_get(&htable_inode, &ino);
}

static ientry_t *get_ientry_by_fid(fid_t fid) {
    return htable_get(&htable_fid, fid);
}

static fuse_ino_t inode_idx = 1;

static inline fuse_ino_t next_inode_idx() {
    return inode_idx++;
}

static void *connect_storage(void *v) {
    storageclt_t *storage = (storageclt_t*) v;

    struct timespec ts = {2, 0};

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    for (;;) {
        if (storage->rpcclt.client == 0 || storage->status != 1) {
            warning("SID: %d (%s) needs reconnection", storage->sid,
                    storage->host);
            if (storageclt_initialize(storage) != 0) {
                warning("storageclt_initialize failed for SID: %d (%s): %s",
                        storage->sid, storage->host, strerror(errno));
            }
        }
        nanosleep(&ts, NULL);
    }
    return 0;
}

static struct stat *mattr_to_stat(mattr_t * attr, struct stat *st) {
    memset(st, 0, sizeof (struct stat));
    st->st_mode = attr->mode;
    st->st_nlink = attr->nlink;
    st->st_size = attr->size;
    st->st_ctime = attr->ctime;
    st->st_atime = attr->atime;
    st->st_mtime = attr->mtime;
    st->st_blksize = ROZOFS_BSIZE;
    st->st_blocks = ((attr->size + 512 - 1) / 512);
    st->st_dev = 0;
    st->st_uid = attr->uid;
    st->st_gid = attr->gid;
    return st;
}

static mattr_t *stat_to_mattr(struct stat *st, mattr_t * attr, int to_set) {
    if (to_set & FUSE_SET_ATTR_MODE)
        attr->mode = st->st_mode;
    if (to_set & FUSE_SET_ATTR_SIZE)
        attr->size = st->st_size;
    //if (to_set & FUSE_SET_ATTR_ATIME)
    //    attr->atime = st->st_atime;
    //if (to_set & FUSE_SET_ATTR_MTIME)
    //    attr->mtime = st->st_mtime;
    if (to_set & FUSE_SET_ATTR_UID)
        attr->uid = st->st_uid;
    if (to_set & FUSE_SET_ATTR_GID)
        attr->gid = st->st_gid;
    return attr;
}

static void rozofs_ll_init(void *userdata, struct fuse_conn_info *conn) {
    int *piped = (int *) userdata;
    char s;
    (void) conn;
    if (piped[1] >= 0) {
        s = 0;
        if (write(piped[1], &s, 1) != 1) {
            warning("rozofs_ll_init: pipe write error: %s", strerror(errno));
        }
        close(piped[1]);
    }
}

void rozofs_ll_link(fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent,
        const char *newname) {
    ientry_t *npie = 0;
    ientry_t *ie = 0;
    mattr_t attrs;
    struct fuse_entry_param fep;
    struct stat stbuf;

    if (strlen(newname) > ROZOFS_FILENAME_MAX) {
        errno = ENAMETOOLONG;
        goto error;
    }

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }

    if (!(npie = get_ientry_by_inode(newparent))) {
        errno = ENOENT;
        goto error;
    }

    if (exportclt_link(&exportclt, ie->fid, npie->fid, (char *) newname,
            &attrs) != 0) {
        goto error;
    }

    if (!(ie = get_ientry_by_fid(attrs.fid))) {
        ie = xmalloc(sizeof (ientry_t));
        memcpy(ie->fid, attrs.fid, sizeof (fid_t));
        ie->inode = next_inode_idx();
        list_init(&ie->list);
        ie->db.size = 0;
        ie->db.p = NULL;
        ie->nlookup = 1;
        put_ientry(ie);
    }
    memset(&fep, 0, sizeof (fep));
    fep.ino = ie->inode;
    mattr_to_stat(&attrs, &stbuf);
    stbuf.st_ino = ie->inode;
    fep.attr_timeout = attr_cache_timeo;
    fep.entry_timeout = entry_cache_timeo;
    memcpy(&fep.attr, &stbuf, sizeof (struct stat));
    ie->nlookup++;
    fuse_reply_entry(req, &fep);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_mknod(fuse_req_t req, fuse_ino_t parent, const char *name,
        mode_t mode, dev_t rdev) {
    ientry_t *ie = 0;
    ientry_t *nie = 0;
    mattr_t attrs;
    struct fuse_entry_param fep;
    struct stat stbuf;
    const struct fuse_ctx *ctx;
    ctx = fuse_req_ctx(req);

    DEBUG("mknod (%lu,%s,%04o,%08lX)\n", (unsigned long int) parent, name,
            (unsigned int) mode, (unsigned long int) rdev);

    if (strlen(name) > ROZOFS_FILENAME_MAX) {
        errno = ENAMETOOLONG;
        goto error;
    }
    if (!(ie = get_ientry_by_inode(parent))) {
        errno = ENOENT;
        goto error;
    }

    if (exportclt_mknod(&exportclt, ie->fid, (char *) name, ctx->uid, ctx->gid,
            mode, &attrs) != 0) {
        goto error;
    }

    if (!(nie = get_ientry_by_fid(attrs.fid))) {
        nie = xmalloc(sizeof (ientry_t));
        memcpy(nie->fid, attrs.fid, sizeof (fid_t));
        nie->inode = next_inode_idx();
        list_init(&nie->list);
        nie->db.size = 0;
        nie->db.p = NULL;
        nie->db.eof = 0;
        nie->db.cookie = 0;
        nie->nlookup = 1;
        put_ientry(nie);
    }
    memset(&fep, 0, sizeof (fep));
    fep.ino = nie->inode;
    mattr_to_stat(&attrs, &stbuf);
    stbuf.st_ino = nie->inode;
    fep.attr_timeout = attr_cache_timeo;
    fep.entry_timeout = entry_cache_timeo;
    memcpy(&fep.attr, &stbuf, sizeof (struct stat));
    nie->nlookup++;
    fuse_reply_entry(req, &fep);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
        mode_t mode) {
    ientry_t *ie = 0;
    ientry_t *nie = 0;
    mattr_t attrs;
    struct fuse_entry_param fep;
    struct stat stbuf;
    const struct fuse_ctx *ctx;

    DEBUG("mkdir (%lu,%s,%04o)\n", (unsigned long int) parent, name,
            (unsigned int) mode);

    ctx = fuse_req_ctx(req);
    mode = (mode | S_IFDIR);

    if (strlen(name) > ROZOFS_FILENAME_MAX) {
        errno = ENAMETOOLONG;
        goto error;
    }
    if (!(ie = get_ientry_by_inode(parent))) {
        errno = ENOENT;
        goto error;
    }
    if (exportclt_mkdir(&exportclt, ie->fid, (char *) name, ctx->uid, ctx->gid,
            mode, &attrs) != 0) {
        goto error;
    }

    if (!(nie = get_ientry_by_fid(attrs.fid))) {
        nie = xmalloc(sizeof (ientry_t));
        memcpy(nie->fid, attrs.fid, sizeof (fid_t));
        nie->inode = next_inode_idx();
        list_init(&nie->list);
        nie->db.size = 0;
        nie->db.p = NULL;
        nie->db.eof = 0;
        nie->db.cookie = 0;
        nie->nlookup = 1;
        put_ientry(nie);
    }

    memset(&fep, 0, sizeof (fep));
    fep.ino = nie->inode;
    mattr_to_stat(&attrs, &stbuf);
    stbuf.st_ino = nie->inode;
    fep.attr_timeout = attr_cache_timeo;
    fep.entry_timeout = direntry_cache_timeo;
    memcpy(&fep.attr, &stbuf, sizeof (struct stat));
    nie->nlookup++;

    fuse_reply_entry(req, &fep);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_rename(fuse_req_t req, fuse_ino_t parent, const char *name,
        fuse_ino_t newparent, const char *newname) {
    ientry_t *pie = 0;
    ientry_t *npie = 0;
    ientry_t *old_ie = 0;
    fid_t fid;

    DEBUG("rename (%lu,%s,%lu,%s)\n", (unsigned long int) parent, name,
            (unsigned long int) newparent, newname);

    if (strlen(name) > ROZOFS_FILENAME_MAX ||
            strlen(newname) > ROZOFS_FILENAME_MAX) {
        errno = ENAMETOOLONG;
        goto error;
    }
    if (!(pie = get_ientry_by_inode(parent))) {
        errno = ENOENT;
        goto error;
    }
    if (!(npie = get_ientry_by_inode(newparent))) {
        errno = ENOENT;
        goto error;
    }

    if (exportclt_rename(&exportclt, pie->fid, (char *) name, npie->fid,
            (char *) newname, &fid) != 0) {
        goto error;
    }

    if ((old_ie = get_ientry_by_fid(fid))) {
        old_ie->nlookup--;
    }
    fuse_reply_err(req, 0);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_readlink(fuse_req_t req, fuse_ino_t ino) {
    char target[PATH_MAX];
    ientry_t *ie = NULL;

    DEBUG("readlink (%lu)\n", (unsigned long int) ino);

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }
    if (exportclt_readlink(&exportclt, ie->fid, target) != 0) {
        goto error;
    }

    fuse_reply_readlink(req, (char *) target);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
    file_t *file;
    ientry_t *ie = 0;

    DEBUG("open (%lu)\n", (unsigned long int) ino);

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }
    if (!(file = file_open(&exportclt, ie->fid, S_IRWXU))) {
        goto error;
    }

    fi->fh = (unsigned long) file;
    fuse_reply_open(req, fi);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
        struct fuse_file_info *fi) {
    size_t length = 0;
    char *buff;
    ientry_t *ie = 0;

    DEBUG("read to inode %lu %llu bytes at position %llu\n",
            (unsigned long int) ino, (unsigned long long int) size,
            (unsigned long long int) off);

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }

    file_t *file = (file_t *) (unsigned long) fi->fh;

    memcpy(file->fid, ie->fid, sizeof (fid_t));

    buff = 0;
    length = file_read(file, off, &buff, size);
    if (length == -1)
        goto error;
    fuse_reply_buf(req, (char *) buff, length);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_write(fuse_req_t req, fuse_ino_t ino, const char *buf,
        size_t size, off_t off, struct fuse_file_info *fi) {
    size_t length = 0;
    ientry_t *ie = 0;

    DEBUG("write to inode %lu %llu bytes at position %llu\n",
            (unsigned long int) ino, (unsigned long long int) size,
            (unsigned long long int) off);

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }

    file_t *file = (file_t *) (unsigned long) fi->fh;
    memcpy(file->fid, ie->fid, sizeof (fid_t));

    length = file_write(file, off, buf, size);
    if (length == -1)
        goto error;
    fuse_reply_write(req, length);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_flush(fuse_req_t req, fuse_ino_t ino,
        struct fuse_file_info *fi) {
    file_t *f;
    ientry_t *ie = 0;
    DEBUG_FUNCTION;

    // Sanity check
    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }

    if (!(f = (file_t *) (unsigned long) fi->fh)) {
        errno = EBADF;
        goto out;
    }

    memcpy(f->fid, ie->fid, sizeof (fid_t));

    if (file_flush(f) != 0) {
        goto error;
    }

    fuse_reply_err(req, 0);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_access(fuse_req_t req, fuse_ino_t ino, int mask) {
    DEBUG("access (%lu)\n", (unsigned long int) ino);
    fuse_reply_err(req, 0);
}

void rozofs_ll_release(fuse_req_t req, fuse_ino_t ino,
        struct fuse_file_info *fi) {
    file_t *f;
    ientry_t *ie = 0;
    DEBUG("release (%lu)\n", (unsigned long int) ino);

    // Sanity check
    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }

    if (!(f = (file_t *) (unsigned long) fi->fh)) {
        errno = EBADF;
        goto out;
    }

    memcpy(f->fid, ie->fid, sizeof (fid_t));

    if (file_flush(f) != 0) {
        goto error;
    }

    if (file_close(&exportclt, f) != 0)
        goto error;

    fuse_reply_err(req, 0);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_statfs(fuse_req_t req, fuse_ino_t ino) {
    (void) ino;
    estat_t estat;
    struct statvfs st;

    memset(&st, 0, sizeof (struct statvfs));
    if (exportclt_stat(&exportclt, &estat) == -1)
        goto error;

    st.f_blocks = estat.blocks; // + estat.bfree;
    st.f_bavail = st.f_bfree = estat.bfree;
    st.f_frsize = st.f_bsize = estat.bsize;
    st.f_favail = st.f_ffree = estat.ffree;
    st.f_files = estat.files;
    st.f_namemax = estat.namemax;

    fuse_reply_statfs(req, &st);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_getattr(fuse_req_t req, fuse_ino_t ino,
        struct fuse_file_info *fi) {
    struct stat stbuf;
    (void) fi;
    ientry_t *ie = 0;
    mattr_t attr;

    DEBUG("getattr for inode: %lu\n", (unsigned long int) ino);

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }

    if (exportclt_getattr(&exportclt, ie->fid, &attr) == -1) {
        goto error;
    }

    mattr_to_stat(&attr, &stbuf);
    stbuf.st_ino = ino;
    fuse_reply_attr(req, &stbuf, attr_cache_timeo);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *stbuf,
        int to_set, struct fuse_file_info *fi) {
    ientry_t *ie = 0;
    struct stat o_stbuf;
    mattr_t attr;
    const struct fuse_ctx *ctx;

    DEBUG("setattr for inode: %lu\n", (unsigned long int) ino);

    ctx = fuse_req_ctx(req);

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }

    /*
    if (exportclt_getattr(&exportclt, ie->fid, &attr) == -1) {
        goto error;
    }
     */

    if (exportclt_setattr(&exportclt, ie->fid, stat_to_mattr(stbuf, &attr,
            to_set), to_set) == -1) {
        goto error;
    }

    mattr_to_stat(&attr, &o_stbuf);
    o_stbuf.st_ino = ino;
    fuse_reply_attr(req, &o_stbuf, attr_cache_timeo);

    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_symlink(fuse_req_t req, const char *link, fuse_ino_t parent,
        const char *name) {
    ientry_t *ie = 0;
    mattr_t attrs;
    ientry_t *nie = 0;
    struct fuse_entry_param fep;
    struct stat stbuf;

    DEBUG("symlink (%s,%lu,%s)", link, (unsigned long int) parent, name);

    if (strlen(name) > ROZOFS_FILENAME_MAX) {
        errno = ENAMETOOLONG;
        goto error;
    }

    if (!(ie = get_ientry_by_inode(parent))) {
        errno = ENOENT;
        goto error;
    }

    if (exportclt_symlink(&exportclt, (char *) link, ie->fid, (char *) name,
            &attrs) != 0) {
        goto error;
    }

    if (!(nie = get_ientry_by_fid(attrs.fid))) {
        nie = xmalloc(sizeof (ientry_t));
        memcpy(nie->fid, attrs.fid, sizeof (fid_t));
        nie->inode = next_inode_idx();
        list_init(&nie->list);
        nie->db.size = 0;
        nie->db.p = NULL;
        nie->db.eof = 0;
        nie->db.cookie = 0;
        nie->nlookup = 1;
        put_ientry(nie);
    }
    memset(&fep, 0, sizeof (fep));
    fep.ino = nie->inode;
    mattr_to_stat(&attrs, &stbuf);
    stbuf.st_ino = nie->inode;
    fep.attr_timeout = attr_cache_timeo;
    fep.entry_timeout = entry_cache_timeo;
    memcpy(&fep.attr, &stbuf, sizeof (struct stat));
    nie->nlookup++;
    fuse_reply_entry(req, &fep);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_rmdir(fuse_req_t req, fuse_ino_t parent, const char *name) {
    ientry_t *ie = 0;
    ientry_t *ie2 = 0;
    fid_t fid;

    DEBUG("rmdir (%lu,%s)\n", (unsigned long int) parent, name);

    if (strlen(name) > ROZOFS_FILENAME_MAX) {
        errno = ENAMETOOLONG;
        goto error;
    }
    if (!(ie = get_ientry_by_inode(parent))) {
        errno = ENOENT;
        goto error;
    }
    if (exportclt_rmdir(&exportclt, ie->fid, (char *) name, &fid) != 0) {
        goto error;
    }

    if ((ie2 = get_ientry_by_fid(fid))) {
        ie2->nlookup--;
    }
    fuse_reply_err(req, 0);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_unlink(fuse_req_t req, fuse_ino_t parent, const char *name) {
    ientry_t *ie = 0;
    ientry_t *ie2 = 0;
    fid_t fid;

    DEBUG("unlink (%lu,%s)\n", (unsigned long int) parent, name);

    if (!(ie = get_ientry_by_inode(parent))) {
        errno = ENOENT;
        goto error;
    }
    if (exportclt_unlink(&exportclt, ie->fid, (char *) name, &fid) != 0) {
        goto error;
    }

    if ((ie2 = get_ientry_by_fid(fid))) {
        ie2->nlookup --;
    }
    fuse_reply_err(req, 0);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name, 
        fuse_ino_t ino, mattr_t * attrs) {
    // Get oldsize of buffer
    size_t oldsize = b->size;
    // Set the inode number in stbuf
    struct stat stbuf;
    mattr_to_stat(attrs, &stbuf);
    stbuf.st_ino = ino;
    // Get the size for this entry
    b->size += fuse_add_direntry(req, NULL, 0, name, &stbuf, 0);
    // Realloc dirbuf
    b->p = (char *) realloc(b->p, b->size);
    // Add this entry
    fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf, b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, struct dirbuf *b, off_t off, 
        size_t maxsize) {
    if (off < b->size) {
        return fuse_reply_buf(req, b->p + off, min(b->size - off, maxsize));
    } else {
        // At the end
        // Free buffer
        if (b->p != NULL) {
            free(b->p);
            b->size = 0;
            b->eof = 0;
            b->cookie = 0;
            b->p = NULL;
        }
        return fuse_reply_buf(req, NULL, 0);
    }
}

void rozofs_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
        struct fuse_file_info *fi) {
    ientry_t *ie = 0;
    child_t *child = NULL;
    child_t *iterator = NULL;
    child_t *free_it = NULL;

    DEBUG("readdir (%lu,size:%lu,off:%lu)\n", (unsigned long int) ino, size, 
            off);

    // Get ientry
    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }

    // If the requested size is greater than the current size of buffer
    // and the end of stream is not reached:
    // we send a readdir request
    if ((off + size) > ie->db.size && ie->db.eof == 0) {

        // Send readdir request
        // cookie is a uint64_t.
        // It's the index of next directory entry
        if (exportclt_readdir(&exportclt, ie->fid, &ie->db.cookie, &child, &ie->db.eof) != 0) {
            goto error;
        }

        iterator = child;

        // Process the list of children
        while (iterator != NULL) {
            mattr_t attrs;
            memset(&attrs, 0, sizeof (mattr_t));
            ientry_t *ie2 = 0;

            // May be already cached
            if (!(ie2 = get_ientry_by_fid(iterator->fid))) {
                // If not, cache it
                ie2 = xmalloc(sizeof (ientry_t));
                memcpy(ie2->fid, iterator->fid, sizeof (fid_t));
                ie2->inode = next_inode_idx();
                list_init(&ie2->list);
                ie2->db.size = 0;
                ie2->db.cookie = 0;
                ie2->db.eof = 0;
                ie2->db.p = NULL;
                ie2->nlookup = 1;
                put_ientry(ie2);
            }

            memcpy(attrs.fid, iterator->fid, sizeof (fid_t));

            // Add this directory entry to the buffer
            dirbuf_add(req, &ie->db, iterator->name, ie2->inode, &attrs);

            // Free it and go to the next child
            free_it = iterator;
            iterator = iterator->next;
            free(free_it->name);
            free(free_it);

            // If we reached the end of this current child list but the
            // end of stream is not reached and the requested size is greater
            // than the current size of buffer then send another request
            if (iterator == NULL && ie->db.eof == 0 && ((off + size) > ie->db.size)) {

                if (exportclt_readdir(&exportclt, ie->fid, &ie->db.cookie, &child, &ie->db.eof) != 0) {
                    goto error;
                }
                iterator = child;
            }
        }
    }
    // Reply with data
    reply_buf_limited(req, &ie->db, off, size);

    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name) {
    struct fuse_entry_param fep;
    ientry_t *ie = 0;
    ientry_t *nie = 0;
    struct stat stbuf;
    mattr_t attrs;

    DEBUG("lookup (%lu,%s)\n", (unsigned long int) parent, name);

    if (strlen(name) > ROZOFS_FILENAME_MAX) {
        errno = ENAMETOOLONG;
        goto error;
    }
    if (!(ie = get_ientry_by_inode(parent))) {
        errno = ENOENT;
        goto error;
    }

    if (exportclt_lookup(&exportclt, ie->fid, (char *) name, &attrs) != 0) {
        goto error;
    }

    if (!(nie = get_ientry_by_fid(attrs.fid))) {
        nie = xmalloc(sizeof (ientry_t));
        memcpy(nie->fid, attrs.fid, sizeof (fid_t));
        nie->inode = next_inode_idx();
        list_init(&nie->list);
        nie->db.size = 0;
        nie->db.p = NULL;
        nie->db.eof = 0;
        nie->db.cookie = 0;
        nie->nlookup = 1;
        put_ientry(nie);
    }
    memset(&fep, 0, sizeof (fep));
    mattr_to_stat(&attrs, &stbuf);
    stbuf.st_ino = nie->inode;
    fep.ino = nie->inode;
    fep.attr_timeout = attr_cache_timeo;
    fep.entry_timeout = entry_cache_timeo;
    memcpy(&fep.attr, &stbuf, sizeof (struct stat));
    nie->nlookup++;
    fuse_reply_entry(req, &fep);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_create(fuse_req_t req, fuse_ino_t parent, const char *name,
        mode_t mode, struct fuse_file_info *fi) {
    ientry_t *ie = 0;
    ientry_t *nie = 0;
    mattr_t attrs;
    struct fuse_entry_param fep;
    struct stat stbuf;
    file_t *file;
    const struct fuse_ctx *ctx;

    DEBUG("create (%lu,%s,%04o)\n", (unsigned long int) parent, name,
            (unsigned int) mode);

    ctx = fuse_req_ctx(req);

    if (strlen(name) > ROZOFS_FILENAME_MAX) {
        errno = ENAMETOOLONG;
        goto error;
    }
    if (!(ie = get_ientry_by_inode(parent))) {
        errno = ENOENT;
        goto error;
    }
    if (exportclt_mknod(&exportclt, ie->fid, (char *) name, ctx->uid, ctx->gid,
            mode, &attrs) != 0) {
        goto error;
    }

    if (!(nie = get_ientry_by_fid(attrs.fid))) {
        nie = xmalloc(sizeof (ientry_t));
        memcpy(nie->fid, attrs.fid, sizeof (fid_t));
        nie->inode = next_inode_idx();
        list_init(&nie->list);
        nie->db.size = 0;
        nie->db.p = NULL;
        nie->db.eof = 0;
        nie->db.cookie = 0;
        nie->nlookup = 1;
        put_ientry(nie);
    }

    if (!(file = file_open(&exportclt, nie->fid, S_IRWXU))) {
        goto error;
    }

    memset(&fep, 0, sizeof (fep));
    mattr_to_stat(&attrs, &stbuf);
    stbuf.st_ino = nie->inode;
    fep.ino = nie->inode;
    fep.attr_timeout = attr_cache_timeo;
    fep.entry_timeout = entry_cache_timeo;
    memcpy(&fep.attr, &stbuf, sizeof (struct stat));
    fi->fh = (unsigned long) file;
    nie->nlookup++;
    fuse_reply_create(req, &fep, fi);

    goto out;
error:
    fuse_reply_err(req, errno);
out:
    return;
}

void rozofs_ll_forget(fuse_req_t req, fuse_ino_t ino, unsigned long nlookup) {
    ientry_t *ie;
    DEBUG("forget :%lu, nlookup: %lu", ino, nlookup);
    if ((ie = get_ientry_by_inode(ino))) {
        assert(ie->nlookup >= nlookup);
        DEBUG("forget :%lu, ie lookup: %lu", ino, ie->nlookup);
        if ((ie->nlookup -= nlookup) == 0) {
            DEBUG("del entry for %lu", ino);
            del_ientry(ie);
            free(ie);
        }
    }
}

static struct fuse_lowlevel_ops rozofs_ll_operations = {
    .init = rozofs_ll_init,
    //.destroy = rozofs_ll_destroy,
    .lookup = rozofs_ll_lookup,
    .forget = rozofs_ll_forget,
    .getattr = rozofs_ll_getattr,
    .setattr = rozofs_ll_setattr,
    .readlink = rozofs_ll_readlink,
    .mknod = rozofs_ll_mknod,
    .mkdir = rozofs_ll_mkdir,
    .unlink = rozofs_ll_unlink,
    .rmdir = rozofs_ll_rmdir,
    .symlink = rozofs_ll_symlink,
    .rename = rozofs_ll_rename,
    .open = rozofs_ll_open,
    .link = rozofs_ll_link,
    .read = rozofs_ll_read,
    .write = rozofs_ll_write,
    .flush = rozofs_ll_flush,
    .release = rozofs_ll_release,
    //.opendir = rozofs_ll_opendir,
    .readdir = rozofs_ll_readdir,
    //.releasedir = rozofs_ll_releasedir,
    //.fsyncdir = rozofs_ll_fsyncdir,
    //.chmod = rozofs_ll_chmod,
    .statfs = rozofs_ll_statfs,
    //.setxattr = rozofs_ll_setxattr,
    //.getxattr = rozofs_ll_getxattr,
    //.listxattr = rozofs_ll_listxattr,
    //.removexattr = rozofs_ll_removexattr,
    .access = rozofs_ll_access,
    //.create = rozofs_ll_create,
    //.getlk = rozofs_ll_getlk,
    //.setlk = rozofs_ll_setlk,
    //.bmap = rozofs_ll_bmap,
    //.ioctl = rozofs_ll_ioctl,
    //.poll = rozofs_ll_poll,
};

int fuseloop(struct fuse_args *args, const char *mountpoint, int fg) {
    int i;
    char s;
    int piped[2];
    piped[0] = piped[1] = -1;
    int err;
    struct fuse_chan *ch;
    struct fuse_session *se;
    list_t *p;

    openlog("rozofsmount", LOG_PID, LOG_LOCAL0);

    if (exportclt_initialize (&exportclt, conf.host, conf.export, conf.passwd,
            conf.buf_size * 1024, conf.max_retry) != 0) {
        fprintf(stderr,
                "rozofsmount failed for:\n" "export directory: %s\n"
                "export hostname: %s\n" "local mountpoint: %s\n" "error: %s\n"
                "See log for more information\n", conf.export, conf.host,
                mountpoint, strerror(errno));
        return 1;
    }

    list_init(&inode_entries);
    htable_initialize(&htable_inode, INODE_HSIZE, fuse_ino_hash, fuse_ino_cmp);
    htable_initialize(&htable_fid, PATH_HSIZE, fid_hash, fid_cmp);
    ientry_t *root = xmalloc(sizeof (ientry_t));
    memcpy(root->fid, exportclt.rfid, sizeof (fid_t));
    root->inode = next_inode_idx();
    root->db.size = 0;
    root->db.p = NULL;
    root->nlookup = 1;
    put_ientry(root);

    info("mounting - export: %s from : %s on: %s", conf.export, conf.host,
            mountpoint);

    if (fg == 0) {
        if (pipe(piped) < 0) {
            fprintf(stderr, "pipe error\n");
            return 1;
        }
        err = fork();
        if (err < 0) {
            fprintf(stderr, "fork error\n");
            return 1;
        } else if (err > 0) {
            // Parent process closes up output side of pipe
            close(piped[1]);
            err = read(piped[0], &s, 1);
            if (err == 0) {
                s = 1;
            }
            return s;
        }
        // Child process closes up input side of pipe 
        close(piped[0]);
        s = 1;
    }
    if ((ch = fuse_mount(mountpoint, args)) == NULL) {
        fprintf(stderr, "error in fuse_mount\n");
        if (piped[1] >= 0) {
            if (write(piped[1], &s, 1) != 1) {
                fprintf(stderr, "pipe write error\n");
            }
            close(piped[1]);
        }
        return 1;
    }

    se = fuse_lowlevel_new(args, &rozofs_ll_operations,
            sizeof (rozofs_ll_operations), (void *) piped);

    if (se == NULL) {
        fuse_unmount(mountpoint, ch);
        fprintf(stderr, "error in fuse_lowlevel_new\n");
        usleep(100000); // time for print other error messages by FUSE
        if (piped[1] >= 0) {
            if (write(piped[1], &s, 1) != 1) {
                fprintf(stderr, "pipe write error\n");
            }
            close(piped[1]);
        }
        return 1;
    }

    fuse_session_add_chan(se, ch);

    if (fuse_set_signal_handlers(se) < 0) {
        fprintf(stderr, "error in fuse_set_signal_handlers\n");
        fuse_session_remove_chan(ch);
        fuse_session_destroy(se);
        fuse_unmount(mountpoint, ch);
        if (piped[1] >= 0) {
            if (write(piped[1], &s, 1) != 1) {
                fprintf(stderr, "pipe write error\n");
            }
            close(piped[1]);
        }
        return 1;
    }

    if (fg == 0) {
        setsid();
        setpgid(0, getpid());
        if ((i = open("/dev/null", O_RDWR, 0)) != -1) {
            (void) dup2(i, STDIN_FILENO);
            (void) dup2(i, STDOUT_FILENO);
            (void) dup2(i, STDERR_FILENO);
            if (i > 2)
                close(i);
        }
    }

    // Creates one thread for each storage.
    // Each thread will detect if a storage is going offline
    // and try to reconnect it.

    // For each cluster
    list_for_each_forward(p, &exportclt.mcs) {
        mcluster_t *cluster = list_entry(p, mcluster_t, list);

        // For each storage
        for (i = 0; i < cluster->nb_ms; i++) {
            pthread_t thread;
            if (pthread_create(&thread, NULL, connect_storage, &cluster->ms[i])
                    != 0) {
                severe("can't create connexion thread: %s", strerror(errno));
            }
        }
    }

    err = fuse_session_loop(se);

    if (err) {
        if (piped[1] >= 0) {
            if (write(piped[1], &s, 1) != 1) {
                syslog(LOG_ERR, "pipe write error: %s", strerror(errno));
            }
            close(piped[1]);
        }
    }
    fuse_remove_signal_handlers(se);
    fuse_session_remove_chan(ch);
    fuse_session_destroy(se);
    fuse_unmount(mountpoint, ch);
    exportclt_release(&exportclt);
    ientries_release();
    rozofs_release();

    return err ? 1 : 0;
}

int main(int argc, char *argv[]) {
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    char *mountpoint;
    int fg = 0;
    int res;

    memset(&conf, 0, sizeof (conf));

    conf.max_retry = 5;
    conf.buf_size = 0;

    if (fuse_opt_parse(&args, &conf, rozofs_opts, myfs_opt_proc) < 0) {
        exit(1);
    }

    if (conf.host == NULL) {
        conf.host = strdup("rozofsexport");
    }

    if (conf.export == NULL) {
        conf.export = strdup("/srv/rozo/exports/export");
    }

    if (conf.passwd == NULL) {
        conf.passwd = strdup("none");
    }

    if (conf.buf_size == 0) {
        conf.buf_size = 256;
    }
    if (conf.buf_size < 128) {
        fprintf(stderr,
                "write cache size to low (%u KiB) - increased to 128 KiB\n",
                conf.buf_size);
        conf.buf_size = 128;
    }
    if (conf.buf_size > 8192) {
        fprintf(stderr,
                "write cache size to big (%u KiB) - decreased to 8192 KiB\n",
                conf.buf_size);
        conf.buf_size = 8192;
    }

    if (fuse_version() < 28) {
        if (fuse_opt_add_arg(&args, "-o" FUSE27_DEFAULT_OPTIONS) == -1) {
            fprintf(stderr, "fuse_opt_add_arg failed\n");
            return 1;
        }
    } else {
        if (fuse_opt_add_arg(&args, "-o" FUSE28_DEFAULT_OPTIONS) == -1) {
            fprintf(stderr, "fuse_opt_add_arg failed\n");
            return 1;
        }
    }

    if (fuse_parse_cmdline(&args, &mountpoint, NULL, &fg) == -1) {
        fprintf(stderr, "see: %s -h for help\n", argv[0]);
        return 1;
    }

    if (!mountpoint) {
        fprintf(stderr, "no mount point\nsee: %s -h for help\n", argv[0]);
        return 1;
    }

    res = fuseloop(&args, mountpoint, fg);

    fuse_opt_free_args(&args);
    return res;
}
