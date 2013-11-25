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

#ifndef ROZOFSMOUNT_H
#define ROZOFSMOUNT_H

#define FUSE_USE_VERSION 26
#include <fuse/fuse_lowlevel.h>

#include <rozofs/common/htable.h>
#include <rozofs/rpc/rozofs_rpc_util.h>
#include <rozofs/core/rozofs_tx_common.h>

#include "file.h"

#define hash_xor8(n)    (((n) ^ ((n)>>8) ^ ((n)>>16) ^ ((n)>>24)) & 0xff)
#define ROOT_INODE 1

extern exportclt_t exportclt;

extern list_t inode_entries;
extern htable_t htable_inode;
extern htable_t htable_fid;
extern uint64_t rozofs_ientries_count;

extern double direntry_cache_timeo ;
extern double entry_cache_timeo ;
extern double attr_cache_timeo ;
extern int rozofs_cache_mode;
extern int rozofs_mode;
extern int rozofs_rotation_read_modulo;
extern int rozofs_bugwatch;

typedef struct rozofsmnt_conf {
    char *host;
    char *export;
    char *passwd;
    unsigned buf_size;
    unsigned min_read_size;
    unsigned nbstorcli;    
    unsigned max_retry;
    unsigned dbg_port;  /**< lnkdebug base port: rozofsmount=dbg_port, storcli(1)=dbg_port+1, ....  */
    unsigned instance;  /**< rozofsmount instance: needed when more than 1 rozofsmount run the same server and exports the same filesystem */
    unsigned export_timeout;
    unsigned storcli_timeout;
    unsigned storage_timeout;
    unsigned fs_mode; /**< rozofs mode: 0-> file system/ 1-> block mode */
    unsigned cache_mode;  /**< 0: no option, 1: direct_read, 2: keep_cache */
    unsigned attr_timeout;
    unsigned entry_timeout;
    unsigned nb_cores;
    unsigned shaper;
    unsigned rotate;
    unsigned posix_file_lock;    
    unsigned bsd_file_lock;  
    unsigned max_write_pending ; /**< Maximum number pending write */
} rozofsmnt_conf_t;

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
    uint64_t size;   /**< size of the file */
    dirbuf_t db; ///< buffer used for directory listing
    unsigned long nlookup; ///< number of lookup done on this entry (used for forget)
    mattr_t attrs;   /**< attributes caching for fs_mode = block mode   */
    list_t list;
} ientry_t;

static inline uint32_t fuse_ino_hash(void *n) {
    return hash_xor8(*(uint32_t *) n);
}

static inline int fuse_ino_cmp(void *v1, void *v2) {
    return (*(fuse_ino_t *) v1 - *(fuse_ino_t *) v2);
}

static inline int fid_cmp(void *key1, void *key2) {
    return memcmp(key1, key2, sizeof (fid_t));
}

static inline unsigned int fid_hash(void *key) {
    uint32_t hash = 0;
    uint8_t *c;
    for (c = key; c != key + 16; c++)
        hash = *c + (hash << 6) + (hash << 16) - hash;
    return hash;
}

static inline void ientries_release() {
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
    rozofs_ientries_count++;
    htable_put(&htable_inode, &ie->inode, ie);
    htable_put(&htable_fid, ie->fid, ie);
    list_push_front(&inode_entries, &ie->list);
}

static inline void del_ientry(ientry_t * ie) {
    DEBUG("del inode: %lu\n", ie->inode);
    rozofs_ientries_count--;
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

static inline ientry_t *alloc_ientry(fid_t fid) {
	ientry_t *ie;

	ie = xmalloc(sizeof(ientry_t));
	memcpy(ie->fid, fid, sizeof(fid_t));
	ie->inode = fid_hash(fid);
	ie->size = 0;
	list_init(&ie->list);
	ie->db.size = 0;
	ie->db.eof = 0;
	ie->db.cookie = 0;
	ie->db.p = NULL;
	ie->nlookup = 1;
	put_ientry(ie);

	return ie;
}

static inline struct stat *mattr_to_stat(mattr_t * attr, struct stat *st) {
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

static inline mattr_t *stat_to_mattr(struct stat *st, mattr_t * attr,
		int to_set) {
	if (to_set & FUSE_SET_ATTR_MODE)
		attr->mode = st->st_mode;
	if (to_set & FUSE_SET_ATTR_SIZE)
		attr->size = st->st_size;
	if (to_set & FUSE_SET_ATTR_ATIME)
		attr->atime = st->st_atime;
	if (to_set & FUSE_SET_ATTR_MTIME)
		attr->mtime = st->st_mtime;
	if (to_set & FUSE_SET_ATTR_UID)
		attr->uid = st->st_uid;
	if (to_set & FUSE_SET_ATTR_GID)
		attr->gid = st->st_gid;
	return attr;
}

/*Export commands prototypes*/
void rozofs_ll_getattr_nb(fuse_req_t req, fuse_ino_t ino,
		struct fuse_file_info *fi);

void rozofs_ll_setattr_nb(fuse_req_t req, fuse_ino_t ino, struct stat *stbuf,
		int to_set, struct fuse_file_info *fi);

void rozofs_ll_lookup_nb(fuse_req_t req, fuse_ino_t parent, const char *name);

void rozofs_ll_mkdir_nb(fuse_req_t req, fuse_ino_t parent, const char *name,
		mode_t mode);

void rozofs_ll_mknod_nb(fuse_req_t req, fuse_ino_t parent, const char *name,
		mode_t mode, dev_t rdev);

void rozofs_ll_open_nb(fuse_req_t req, fuse_ino_t ino,
		struct fuse_file_info *fi);

void rozofs_ll_symlink_nb(fuse_req_t req, const char *link, fuse_ino_t parent,
		const char *name);

void rozofs_ll_readlink_nb(fuse_req_t req, fuse_ino_t ino);

void rozofs_ll_link_nb(fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent,
		const char *newname);

void rozofs_ll_unlink_nb(fuse_req_t req, fuse_ino_t parent, const char *name);

void rozofs_ll_rmdir_nb(fuse_req_t req, fuse_ino_t parent, const char *name);

void rozofs_ll_rename_nb(fuse_req_t req, fuse_ino_t parent, const char *name,
		fuse_ino_t newparent, const char *newname);

void rozofs_ll_statfs_nb(fuse_req_t req, fuse_ino_t ino);

void rozofs_ll_create_nb(fuse_req_t req, fuse_ino_t parent, const char *name,
		mode_t mode, struct fuse_file_info *fi);

void rozofs_ll_setxattr_nb(fuse_req_t req, fuse_ino_t ino, const char *name,
		const char *value, size_t size, int flags);

void rozofs_ll_getxattr_nb(fuse_req_t req, fuse_ino_t ino, const char *name,
		size_t size);

void rozofs_ll_removexattr_nb(fuse_req_t req, fuse_ino_t ino, const char *name);

void rozofs_ll_listxattr_nb(fuse_req_t req, fuse_ino_t ino, size_t size);

void rozofs_ll_readdir_nb(fuse_req_t req, fuse_ino_t ino, size_t size,
		off_t off, struct fuse_file_info *fi);

void rozofs_ll_read_nb(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
		struct fuse_file_info *fi);

void rozofs_ll_write_nb(fuse_req_t req, fuse_ino_t ino, const char *buf,
		size_t size, off_t off, struct fuse_file_info *fi);

void rozofs_ll_flush_nb(fuse_req_t req, fuse_ino_t ino,
		struct fuse_file_info *fi);

void rozofs_ll_release_nb(fuse_req_t req, fuse_ino_t ino,
		struct fuse_file_info *fi);

void rozofs_ll_getlk_nb(fuse_req_t req, fuse_ino_t ino,
		struct fuse_file_info *fi, struct flock *lock);

void rozofs_ll_setlk_nb(fuse_req_t req, fuse_ino_t ino,
		struct fuse_file_info *fi, struct flock *lock, int sleep);

void rozofs_ll_flock_nb(fuse_req_t req, fuse_ino_t ino,
		struct fuse_file_info *fi, int op);

void init_write_flush_stat(int max_write_pending);
/*
**__________________________________________________________________
*/
/**
 *  flush the content of the buffer to the disk

 @param fuse_ctx_p: pointer to the fuse transaction context
 @param p : pointer to the file structure that contains buffer information

 @retval len_write >= total data length push to the disk
 @retval < 0 --> error while attempting to initiate a write request towards storcli
 */
int buf_flush(void *fuse_ctx_p,file_t *p);

int export_write_block_asynchrone(void *fuse_ctx_p, file_t *file_p,
		sys_recv_pf_t recv_cbk);

/*
**__________________________________________________________________
*/
/**
 API to clear the buffer after a flush
 If some data is pending in the buffer the clear is not done

 @param *p : pointer to the file structure where read buffer information can be retrieved

 @retval -1 some data to write is pending
 @retval 0 if the read buffer is not empty
 */
int clear_read_data(file_t *p);

#endif
