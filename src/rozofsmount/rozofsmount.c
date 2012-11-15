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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <assert.h>
#include <netinet/tcp.h>

#define FUSE_USE_VERSION 26
#include <fuse/fuse_lowlevel.h>
#include <fuse/fuse_opt.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/list.h>
#include <rozofs/common/log.h>
#include <rozofs/common/htable.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/sclient.h>
#include <rozofs/rpc/mclient.h>
#include <rozofs/rpc/mpproto.h>

#include "config.h"
#include "file.h"

#define hash_xor8(n)    (((n) ^ ((n)>>8) ^ ((n)>>16) ^ ((n)>>24)) & 0xff)
#define INODE_HSIZE 8192
#define PATH_HSIZE  8192

#define FUSE28_DEFAULT_OPTIONS "default_permissions,allow_other,fsname=rozofs,subtype=rozofs,big_writes"
#define FUSE27_DEFAULT_OPTIONS "default_permissions,allow_other,fsname=rozofs,subtype=rozofs"

#define CACHE_TIMEOUT 10.0

#define CONNECTION_THREAD_TIMESPEC  2

static SVCXPRT *rozofsmount_profile_svc = 0;

DEFINE_PROFILING(mpp_profiler_t) = {0};

extern void rozofsmount_profile_program_1(struct svc_req *rqstp, SVCXPRT *ctl_svc);

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
    fprintf(stderr, "\t-E EXPORT_PATH\t\tdefine path of an export see exportd (default: /srv/rozofs/exports/export) equivalent to '-o exportpath=EXPORT_PATH'\n");
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

typedef struct dirbuf {
    char *p;
    size_t size;
    uint8_t eof;
    uint64_t cookie;
} dirbuf_t;

/** entry kept locally to map fuse_inode_t with rozofs fid_t */
typedef struct ientry {
    fuse_ino_t inode; ///< value of the inode allocated by rozofs
    fid_t fid; ///< unique file identifier associated with the file or directory
    dirbuf_t db; ///< buffer used for directory listing
    unsigned long nlookup; ///< number of lookup done on this entry (used for forget)
    list_t list;
} ientry_t;

static exportclt_t exportclt;

static list_t inode_entries;
static htable_t htable_inode;
static htable_t htable_fid;

static inline uint32_t fuse_ino_hash(void *n) {
    return hash_xor8(*(uint32_t *) n);
}

static inline int fuse_ino_cmp(void *v1, void *v2) {
    return (*(fuse_ino_t *) v1 - *(fuse_ino_t *) v2);
}

static inline int fid_cmp(void *key1, void *key2) {
    return memcmp(key1, key2, sizeof (fid_t));
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

static inline void put_ientry(ientry_t * ie) {
    DEBUG("put inode: %lu\n", ie->inode);
    htable_put(&htable_inode, &ie->inode, ie);
    htable_put(&htable_fid, ie->fid, ie);
    list_push_front(&inode_entries, &ie->list);
}

static inline void del_ientry(ientry_t * ie) {
    DEBUG("del inode: %lu\n", ie->inode);
    htable_del(&htable_inode, &ie->inode);
    htable_del(&htable_fid, ie->fid);
    list_remove(&ie->list);
}

static inline ientry_t *get_ientry_by_inode(fuse_ino_t ino) {
    return htable_get(&htable_inode, &ino);
}

static inline ientry_t *get_ientry_by_fid(fid_t fid) {
    return htable_get(&htable_fid, fid);
}

static fuse_ino_t inode_idx = 1;

static inline fuse_ino_t next_inode_idx() {
    return inode_idx++;
}

/** Send a request to a storage node for get the list of TCP ports this storage
 *
 * @param storage: the storage node
 *
 * @return 0 on success otherwise -1
 */
static int get_storage_ports(mstorage_t *s) {
    int status = -1;
    int i = 0;
    mclient_t mclt;

    uint32_t ports[STORAGE_NODE_PORTS_MAX];
    memset(ports, 0, sizeof (uint32_t) * STORAGE_NODE_PORTS_MAX);
    strcpy(mclt.host, s->host);

    /* Initialize connection with storage (by mproto) */
    if (mclient_initialize(&mclt) != 0) {
        severe("Warning: failed to join storage (host: %s), %s.\n",
                s->host, strerror(errno));
        goto out;
    } else {
        /* Send request to get storage TCP ports */
        if (mclient_ports(&mclt, ports) != 0) {
            severe("Warning: failed to get ports for storage (host: %s).\n",
                    s->host);
            goto out;
        }
    }

    /* Copy each TCP ports */
    for (i = 0; i < STORAGE_NODE_PORTS_MAX; i++) {
        if (ports[i] != 0) {
            strcpy(s->sclients[i].host, s->host);
            s->sclients[i].port = ports[i];
            s->sclients[i].status = 0;
            s->sclients_nb++;
        }
    }

    /* Release mclient*/
    mclient_release(&mclt);

    status = 0;
out:
    return status;
}

/** Check if the connections for one storage node are active or not
 *
 * @param storage: the storage node
 */
static void *connect_storage(void *v) {
    mstorage_t *mstorage = (mstorage_t*) v;
    int i = 0;

    struct timespec ts = {CONNECTION_THREAD_TIMESPEC, 0};

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    for (;;) {

        /* We don't have the ports for this storage node */
        if (mstorage->sclients_nb == 0) {
            /* Get ports for this storage node */
            if (get_storage_ports(mstorage) != 0) {
                DEBUG("Cannot get ports for host: %s", mstorage->host);
            }
        }

        /* Verify each connections for this storage node */
        for (i = 0; i < mstorage->sclients_nb; i++) {

            sclient_t *sclt = &mstorage->sclients[i];

            if (sclt->rpcclt.client == 0 || sclt->status != 1) {

                DEBUG("Disconnection (host: %s, port: %u) detected",
                        sclt->host, sclt->port);

                if (sclient_initialize(sclt) != 0) {
                    DEBUG("sclient_initialize failed for connection (host: %s, port: %u): %s",
                            sclt->host, sclt->port, strerror(errno));
                }
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

    START_PROFILING(rozofs_ll_link);

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
        ie->db.eof = 0;
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
    STOP_PROFILING(rozofs_ll_link);
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

    START_PROFILING(rozofs_ll_mknod);

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
    STOP_PROFILING(rozofs_ll_mknod);
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

    START_PROFILING(rozofs_ll_mkdir);

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
    STOP_PROFILING(rozofs_ll_mkdir);
    return;
}

void rozofs_ll_rename(fuse_req_t req, fuse_ino_t parent, const char *name,
        fuse_ino_t newparent, const char *newname) {
    ientry_t *pie = 0;
    ientry_t *npie = 0;
    ientry_t *old_ie = 0;
    fid_t fid;

    START_PROFILING(rozofs_ll_rename);

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
    STOP_PROFILING(rozofs_ll_rename);
    return;
}

void rozofs_ll_readlink(fuse_req_t req, fuse_ino_t ino) {
    char target[PATH_MAX];
    ientry_t *ie = NULL;

    START_PROFILING(rozofs_ll_readlink);

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
    STOP_PROFILING(rozofs_ll_readlink);
    return;
}

void rozofs_ll_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
    file_t *file;
    ientry_t *ie = 0;

    START_PROFILING(rozofs_ll_open);

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
    STOP_PROFILING(rozofs_ll_open);
    return;
}

void rozofs_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
        struct fuse_file_info *fi) {
    size_t length = 0;
    char *buff;
    ientry_t *ie = 0;

    START_PROFILING_IO(rozofs_ll_read, size);

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
    STOP_PROFILING(rozofs_ll_read);
    return;
}

void rozofs_ll_write(fuse_req_t req, fuse_ino_t ino, const char *buf,
        size_t size, off_t off, struct fuse_file_info *fi) {
    size_t length = 0;
    ientry_t *ie = 0;

    START_PROFILING_IO(rozofs_ll_write, size);

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
    STOP_PROFILING(rozofs_ll_write);
    return;
}

void rozofs_ll_flush(fuse_req_t req, fuse_ino_t ino,
        struct fuse_file_info *fi) {
    file_t *f;
    ientry_t *ie = 0;

    START_PROFILING(rozofs_ll_flush);

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
    STOP_PROFILING(rozofs_ll_flush);
    return;
}

void rozofs_ll_access(fuse_req_t req, fuse_ino_t ino, int mask) {
    START_PROFILING(rozofs_ll_access);
    fuse_reply_err(req, 0);
    STOP_PROFILING(rozofs_ll_access);
}

void rozofs_ll_release(fuse_req_t req, fuse_ino_t ino,
        struct fuse_file_info *fi) {
    file_t *f;
    ientry_t *ie = 0;

    START_PROFILING(rozofs_ll_release);

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
    STOP_PROFILING(rozofs_ll_release);
    return;
}

void rozofs_ll_statfs(fuse_req_t req, fuse_ino_t ino) {
    (void) ino;
    estat_t estat;
    struct statvfs st;

    START_PROFILING(rozofs_ll_statfs);

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
    STOP_PROFILING(rozofs_ll_statfs);
    return;
}

void rozofs_ll_getattr(fuse_req_t req, fuse_ino_t ino,
        struct fuse_file_info *fi) {
    struct stat stbuf;
    (void) fi;
    ientry_t *ie = 0;
    mattr_t attr;

    START_PROFILING(rozofs_ll_getattr);

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
    STOP_PROFILING(rozofs_ll_getattr);
    return;
}

void rozofs_ll_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *stbuf,
        int to_set, struct fuse_file_info *fi) {
    ientry_t *ie = 0;
    struct stat o_stbuf;
    mattr_t attr;

    START_PROFILING(rozofs_ll_setattr);

    DEBUG("setattr for inode: %lu\n", (unsigned long int) ino);

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }

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
    STOP_PROFILING(rozofs_ll_setattr);
    return;
}

void rozofs_ll_symlink(fuse_req_t req, const char *link, fuse_ino_t parent,
        const char *name) {
    ientry_t *ie = 0;
    mattr_t attrs;
    ientry_t *nie = 0;
    struct fuse_entry_param fep;
    struct stat stbuf;

    START_PROFILING(rozofs_ll_symlink);

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
    STOP_PROFILING(rozofs_ll_symlink);
    return;
}

void rozofs_ll_rmdir(fuse_req_t req, fuse_ino_t parent, const char *name) {
    ientry_t *ie = 0;
    ientry_t *ie2 = 0;
    fid_t fid;

    START_PROFILING(rozofs_ll_rmdir);

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
    STOP_PROFILING(rozofs_ll_rmdir);
    return;
}

void rozofs_ll_unlink(fuse_req_t req, fuse_ino_t parent, const char *name) {
    ientry_t *ie = 0;
    ientry_t *ie2 = 0;
    fid_t fid;

    START_PROFILING(rozofs_ll_unlink);

    DEBUG("unlink (%lu,%s)\n", (unsigned long int) parent, name);

    if (!(ie = get_ientry_by_inode(parent))) {
        errno = ENOENT;
        goto error;
    }
    if (exportclt_unlink(&exportclt, ie->fid, (char *) name, &fid) != 0) {
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
    STOP_PROFILING(rozofs_ll_unlink);
    return;
}

void rozofs_ll_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
    ientry_t *ie = 0;

    START_PROFILING(rozofs_ll_opendir);

    /* just check if exists */
    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }
    fuse_reply_open(req, fi);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    STOP_PROFILING(rozofs_ll_opendir);
}

static void dirbuf_add(fuse_req_t req, dirbuf_t *b, const char *name,
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

    START_PROFILING(rozofs_ll_readdir);

    DEBUG("readdir (%lu, size:%llu, off:%llu)\n", (unsigned long int) ino,
            (unsigned long long int) size, (unsigned long long int) off);

    // Get ientry
    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }

    // If offset is 0, it maybe the first time the caller read the dir but
    // it might also have already read the a chunk of dir but wants to
    // read from 0 again. it might be overkill but to be sure not using
    // buffer content force exportd readdir call.
    if (off == 0) {
        if (ie->db.p != NULL) {
            free(ie->db.p);
            ie->db.size = 0;
            ie->db.eof = 0;
            ie->db.cookie = 0;
            ie->db.p = NULL;
        }
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
    STOP_PROFILING(rozofs_ll_readdir);
    return;
}

void rozofs_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name) {
    struct fuse_entry_param fep;
    ientry_t *ie = 0;
    ientry_t *nie = 0;
    struct stat stbuf;
    mattr_t attrs;

    START_PROFILING(rozofs_ll_lookup);

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
    STOP_PROFILING(rozofs_ll_lookup);
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

    START_PROFILING(rozofs_ll_create);

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
    STOP_PROFILING(rozofs_ll_create);
    return;
}

void rozofs_ll_forget(fuse_req_t req, fuse_ino_t ino, unsigned long nlookup) {
    ientry_t *ie;

    START_PROFILING(rozofs_ll_forget);

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

    STOP_PROFILING(rozofs_ll_forget);
}

#define XATTR_CAPABILITY_NAME "security.capability"

void rozofs_ll_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
        size_t size) {

    START_PROFILING(rozofs_ll_getxattr);

    DEBUG("getxattr (inode: %lu, name: %s, size: %llu) \n",
            (unsigned long int) ino, name, (unsigned long long int) size);

    /// XXX: respond with the error ENODATA for these calls
    // to avoid that the getxattr called on export at each write to this file
    // But these calls have overhead (each one requires a context switch)
    // It's seems to be a bug in kernel.
    if (strcmp(XATTR_CAPABILITY_NAME, name) == 0) {
        fuse_reply_err(req, ENODATA);
        goto out;
    }

    ientry_t *ie = 0;
    uint64_t value_size = 0;
    char value[ROZOFS_XATTR_VALUE_MAX];

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }

    if (exportclt_getxattr(&exportclt, ie->fid, (char *) name, value, size,
            &value_size) == -1)
        goto error;

    if (size == 0) {
        fuse_reply_xattr(req, value_size);
        goto out;
    }

    fuse_reply_buf(req, (char *) value, value_size);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    STOP_PROFILING(rozofs_ll_getxattr);
    return;
}

void rozofs_ll_setxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
        const char *value, size_t size, int flags) {
    ientry_t *ie = 0;

    START_PROFILING(rozofs_ll_setxattr);

    DEBUG("setxattr (inode: %lu, name: %s, value: %s, size: %llu)\n",
            (unsigned long int) ino, name, value,
            (unsigned long long int) size);

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }

    if (exportclt_setxattr(&exportclt, ie->fid, (char *) name, (char *) value,
            size, flags) == -1)
        goto error;

    fuse_reply_err(req, 0);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    STOP_PROFILING(rozofs_ll_setxattr);
    return;
}

void rozofs_ll_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size) {
    ientry_t *ie = 0;
    uint64_t list_size = 0;
    char list[ROZOFS_XATTR_LIST_MAX];

    START_PROFILING(rozofs_ll_listxattr);

    DEBUG("listxattr (inode: %lu, size: %llu)\n", (unsigned long int) ino,
            (unsigned long long int) size);

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }

    if (exportclt_listxattr(&exportclt, ie->fid, list, size, &list_size) == -1)
        goto error;

    if (size == 0) {
        fuse_reply_xattr(req, list_size);
        goto out;
    }

    fuse_reply_buf(req, (char *) list, list_size);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    STOP_PROFILING(rozofs_ll_listxattr);
    return;
}

void rozofs_ll_removexattr(fuse_req_t req, fuse_ino_t ino, const char *name) {
    ientry_t *ie = 0;

    START_PROFILING(rozofs_ll_removexattr);

    DEBUG("removexattr (inode: %lu, name: %s)\n", (unsigned long int) ino,
            name);

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }

    if (exportclt_removexattr(&exportclt, ie->fid, (char *) name) == -1)
        goto error;

    fuse_reply_err(req, 0);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    STOP_PROFILING(rozofs_ll_removexattr);
    return;
}

/*
 * All below are implemented for monitoring purpose.
 */
#warning fake untested function.

void rozofs_ll_getlk(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi,
        struct flock *lock) {
    START_PROFILING(rozofs_ll_getlk);
    fuse_reply_err(req, 0);
    STOP_PROFILING(rozofs_ll_getlk);
}

#warning fake untested function.

void rozofs_ll_setlk(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi,
        struct flock *lock, int sleep) {
    START_PROFILING(rozofs_ll_getlk);
    fuse_reply_err(req, 0);
    STOP_PROFILING(rozofs_ll_setlk);
}

#warning fake untested function.

void rozofs_ll_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
    START_PROFILING(rozofs_ll_releasedir);
    fuse_reply_err(req, 0);
    STOP_PROFILING(rozofs_ll_releasedir);
}

#warning fake untested function.

void rozofs_ll_fsyncdir(fuse_req_t req, fuse_ino_t ino, int datasync,
        struct fuse_file_info *fi) {
    START_PROFILING(rozofs_ll_fsyncdir);
    fuse_reply_err(req, 0);
    STOP_PROFILING(rozofs_ll_fsyncdir);
}

#warning fake untested function.

void rozofs_ll_ioctl(fuse_req_t req, fuse_ino_t ino, int cmd, void *arg,
        struct fuse_file_info *fi, unsigned flags,
        const void *in_buf, size_t in_bufsz, size_t out_bufsz) {
    START_PROFILING(rozofs_ll_ioctl);
    fuse_reply_ioctl(req, 0, in_buf, out_bufsz);
    STOP_PROFILING(rozofs_ll_ioctl);
}

static SVCXPRT *rozofsmount_create_rpc_service(int port) {
    int sock;
    int one = 1;
    struct sockaddr_in sin;

    /* Give the socket a name. */
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = INADDR_ANY;

    /* Create the socket. */
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        severe("Can't create socket: %s.", strerror(errno));
        return NULL;
    }

    /* Set socket options */
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof (int));
    setsockopt(sock, SOL_TCP, TCP_DEFER_ACCEPT, (char *) &one, sizeof (int));
    setsockopt(sock, SOL_TCP, TCP_NODELAY, (char *) &one, sizeof (int));

    /* Bind the socket */
    if (bind(sock, (struct sockaddr *) &sin, sizeof (struct sockaddr)) < 0) {
        severe("Couldn't bind to tcp port %d", port);
        return NULL;
    }

    /* Creates a TCP/IP-based RPC service transport */
    return svctcp_create(sock, ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE);

}

void rozofmount_profiling_thread_run(void *args) {

    int *port = args;

    rozofsmount_profile_svc = rozofsmount_create_rpc_service(*port);
    if (!rozofsmount_profile_svc) {
        severe("can't create monitoring service: %s", strerror(errno));
    }

    /* Associates STORAGE_PROGRAM and STORAGE_VERSION
     * with the service dispatch procedure, storage_program_1.
     * Here protocol is zero, the service is not registered with
     *  the portmap service */
    if (!svc_register(rozofsmount_profile_svc, ROZOFSMOUNT_PROFILE_PROGRAM,
            ROZOFSMOUNT_PROFILE_VERSION,
            rozofsmount_profile_program_1, 0)) {
        severe("can't register service : %s", strerror(errno));
    }

    svc_run();
    DEBUG("REACHED !!!!");
    /* NOT REACHED */
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
    .opendir = rozofs_ll_opendir,
    .readdir = rozofs_ll_readdir,
    .releasedir = rozofs_ll_releasedir,
    .fsyncdir = rozofs_ll_fsyncdir,
    .statfs = rozofs_ll_statfs,
    .setxattr = rozofs_ll_setxattr,
    .getxattr = rozofs_ll_getxattr,
    .listxattr = rozofs_ll_listxattr,
    .removexattr = rozofs_ll_removexattr,
    .access = rozofs_ll_access,
    .create = rozofs_ll_create,
    .getlk = rozofs_ll_getlk,
    .setlk = rozofs_ll_setlk,
    //.bmap = rozofs_ll_bmap,
    .ioctl = rozofs_ll_ioctl,
    //.poll = rozofs_ll_poll,
};

int fuseloop(struct fuse_args *args, const char *mountpoint, int fg) {
    int i = 0;
    int err;
    int piped[2];
    piped[0] = piped[1] = -1;
    char s;
    struct fuse_chan *ch;
    struct fuse_session *se;
    list_t *p = NULL;
    list_t *iterator = NULL;
    int sock;
    pthread_t profiling_thread;
    uint16_t profiling_port;

    openlog("rozofsmount", LOG_PID, LOG_LOCAL0);

    /* Initiate the connection to the export and get informations
     * about exported filesystem */
    if (exportclt_initialize(
            &exportclt,
            conf.host,
            conf.export,
            conf.passwd,
            conf.buf_size * 1024,
            conf.max_retry) != 0) {
        fprintf(stderr,
                "rozofsmount failed for:\n" "export directory: %s\n"
                "export hostname: %s\n" "local mountpoint: %s\n" "error: %s\n"
                "See log for more information\n", conf.export, conf.host,
                mountpoint, strerror(errno));
        return 1;
    }

    /* Initiate the connection to each storage node (with mproto),
     *  get the list of ports and
     *  establish a connection with each storage socket (with sproto) */
    list_for_each_forward(iterator, &exportclt.storages) {

        mstorage_t *s = list_entry(iterator, mstorage_t, list);

        mclient_t mclt;
        strcpy(mclt.host, s->host);
        uint32_t ports[STORAGE_NODE_PORTS_MAX];
        memset(ports, 0, sizeof (uint32_t) * STORAGE_NODE_PORTS_MAX);

        /* Initialize connection with storage (by mproto) */
        if (mclient_initialize(&mclt) != 0) {
            fprintf(stderr, "Warning: failed to join storage (host: %s), %s.\n",
                    s->host, strerror(errno));
        } else {
            /* Send request to get storage TCP ports */
            if (mclient_ports(&mclt, ports) != 0) {
                fprintf(stderr,
                        "Warning: failed to get ports for storage (host: %s).\n"
                        , s->host);
            }
        }

        /* Initialize each TCP ports connection with this storage node
         *  (by sproto) */
        for (i = 0; i < STORAGE_NODE_PORTS_MAX; i++) {
            if (ports[i] != 0) {
                strcpy(s->sclients[i].host, s->host);
                s->sclients[i].port = ports[i];
                s->sclients[i].status = 0;
                if (sclient_initialize(&s->sclients[i]) != 0) {
                    fprintf(stderr,
                            "Warning: failed to join storage (host: %s, port: %u), %s.\n",
                            s->host, s->sclients[i].port, strerror(errno));
                }
                s->sclients_nb++;
            }
        }

        /* Release mclient*/
        mclient_release(&mclt);
    }

    /* Initialize list and htables for inode_entries*/
    list_init(&inode_entries);
    htable_initialize(&htable_inode, INODE_HSIZE, fuse_ino_hash, fuse_ino_cmp);
    htable_initialize(&htable_fid, PATH_HSIZE, fid_hash, fid_cmp);

    /* Put the root inode entry*/
    ientry_t *root = xmalloc(sizeof (ientry_t));
    memcpy(root->fid, exportclt.rfid, sizeof (fid_t));
    root->inode = next_inode_idx();
    root->db.size = 0;
    root->db.eof = 0;
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

    /* Creates one thread for each storage TCP connection.
     Each thread will detect if a storage connection is going offline
     and try to reconnect it.*/
    list_for_each_forward(p, &exportclt.storages) {

        mstorage_t *storage = list_entry(p, mstorage_t, list);
        pthread_t thread;

        if ((errno = pthread_create(&thread, NULL, connect_storage, storage)) != 0) {
            severe("can't create connexion thread: %s", strerror(errno));
        }
    }

    /*
     * Start profiling server
     */
    gprofiler.uptime = time(0);
    strcpy((char *) gprofiler.vers, VERSION);
    /* Find a free port */
    for (profiling_port = 50000; profiling_port < 60000; profiling_port++) {
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            severe("can't create socket: %s", strerror(errno));
            break;
        }
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(profiling_port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if ((bind(sock, (struct sockaddr *) &addr, sizeof (struct sockaddr_in))) != 0) {
            if (errno == EADDRINUSE)
                profiling_port++; /* Try next port */
            else {
                severe("can't bind socket: %s", strerror(errno));
                close(sock);
            }
        } else {
            shutdown(sock, 2);
            close(sock);
            break;
        }
    }

    if (profiling_port >= 60000) {
        severe("no free port for monitoring !");
    } else {
        if ((errno = pthread_create(&profiling_thread, NULL,
                (void*) rozofmount_profiling_thread_run,
                &profiling_port)) != 0) {
            severe("can't create monitoring thread: %s", strerror(errno));
        }
    }
    info("monitoring port: %d", profiling_port);

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
        conf.export = strdup("/srv/rozofs/exports/export");
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