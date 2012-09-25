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

#ifndef _EXPORT_H
#define _EXPORT_H

/** export API
 *
 * export manages storage of meta data. Rozofs managed several exports but
 * each of them has is responsible for its own meta data which are
 * self-contained and self-sufficient. Any empty repository can be used as
 * an export. The repository will be initialize at exportd startup
 * (@see export_create()). Each repository is a 3 level directories tree.
 * First level (lv1 in the code) is a hash table of fid_t (which should
 * identifies uniquely a rozofs file across exports). Second level contains
 * files (regular or directory) named by fid. Rozofs regular files's meta data
 * are stored in regular files. Rozofs directories leads to directories which
 * contains Third level files theses last correspond to directory entries and
 * meta data needs to find each of them.
 */

#include <limits.h>
#include <sys/stat.h>
#include <uuid/uuid.h>
#include <sys/param.h>

#include "rozofs.h"
#include "volume.h"
#include "dist.h"
#include "cache.h"

#define TRASH_DNAME "trash"
#define FSTAT_FNAME "fstat"
#define CONST_FNAME "const"

#define EXPORT_SET_ATTR_MODE  (1 << 0)
#define EXPORT_SET_ATTR_UID   (1 << 1)
#define EXPORT_SET_ATTR_GID   (1 << 2)
#define EXPORT_SET_ATTR_SIZE  (1 << 3)

/** stat of an export
 * these values are independent of volume
 */
typedef struct export_fstat {
    uint64_t blocks;
    uint64_t files;
} export_fstat_t;

/** export stucture
 *
 */
typedef struct export {
    eid_t eid; ///< export identifier
    volume_t *volume; ///< the volume export relies on
    char root[PATH_MAX]; ///< absolute path of the storage root
    char md5[ROZOFS_MD5_SIZE]; ///< passwd
    uint64_t squota; ///< soft quota in blocks
    uint64_t hquota; ///< hard quota in blocks
    export_fstat_t fstat; ///< fstat value
    int fdstat; ///< open file descriptor on stat file
    fid_t rfid; ///< root fid
    lv2_cache_t *lv2_cache; ///< cache of lv2 entries
    list_t rmfiles; ///< List of files to delete
    pthread_rwlock_t rm_lock; ///< Lock for the list of files to delete
} export_t;

int export_rm_bins(export_t * e);


/** check if export directory is valid
 *
 * @param root: root directory to check
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_is_valid(const char *root);


/** create an export
 *
 * create rozofs system files
 * create a export_stat file an a trash directory
 * generate root uuid create lv2 directory
 *
 * @param root: root directory to create file in
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_create(const char *root);

/** initialize an export
 *
 * @param export: pointer to the export
 * @param volume: pointer to the volume the export relies on
 * @param lv2_cache: pointer to the cache to use
 * @param eid: id of this export
 * @param root: path to root directory
 * @param md5: password
 * @param squota: soft quotas
 * @param: hard quotas
 *
 * @return 0 on success -1 otherwise (errno is set)
 */
int export_initialize(export_t * e, volume_t *volume, lv2_cache_t *lv2_cache,
        eid_t eid, const char *root, const char *md5, uint64_t squota,
        uint64_t hquota);

/** initialize an export.
 *
 * close file descriptors.
 *
 * @param export: pointer to the export
 */
void export_release(export_t * e);

/** stat an export.
 *
 *
 * @param export: pointer to the export
 * @param st: stat to fill in (see estat_t)
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_stat(export_t * e, estat_t * st);

/** lookup file in an export.
 *
 *
 * @param export: pointer to the export
 * @param pfid: fid of the parent of the searched file
 * @param name: pointer to the name of the searched file
 * @param attrs: mattr_t to fill
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_lookup(export_t *e, fid_t pfid, char *name, mattr_t * attrs);

/** get attributes of a managed file
 *
 * @param e: the export managing the file
 * @param fid: the id of the file
 * @param attrs: attributes to fill.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_getattr(export_t *e, fid_t fid, mattr_t * attrs);

/** set attributes of a managed file
 *
 * @param e: the export managing the file
 * @param fid: the id of the file
 * @param attrs: attributes to set.
 * @param to_set: fields to set in attributes
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_setattr(export_t *e, fid_t fid, mattr_t * attrs, int to_set);

/** create a hard link
 *
 * @param e: the export managing the file
 * @param inode: the id of the file we want to be link on
 * @param newparent: parent od the new file (the link)
 * @param newname: the name of the new file
 * @param attrs: mattr_t to fill (used by upper level functions)
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_link(export_t *e, fid_t inode, fid_t newparent, char *newname,
        mattr_t *attrs);

/** create a new file
 *
 * @param e: the export managing the file
 * @param pfid: the id of the parent
 * @param name: the name of this file.
 * @param uid: the user id
 * @param gid: the group id
 * @param mode: mode of this file
 * @param attrs: mattr_t to fill (used by upper level functions)
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_mknod(export_t *e, fid_t pfid, char *name, uint32_t uid,
        uint32_t gid, mode_t mode, mattr_t *attrs);

/** create a new directory
 *
 * @param e: the export managing the file
 * @param pfid: the id of the parent
 * @param name: the name of this file.
 * @param uid: the user id
 * @param gid: the group id
 * @param mode: mode of this file
 * @param attrs: mattr_t to fill (used by upper level functions)
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_mkdir(export_t *e, fid_t pfid, char *name, uint32_t uid,
        uint32_t gid, mode_t mode, mattr_t *attrs);

/** remove a file
 *
 * @param e: the export managing the file
 * @param pfid: the id of the parent
 * @param name: the name of this file.
 * @param fid: the fid of the removed file
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_unlink(export_t * e, fid_t pfid, char *name, fid_t fid);

/*
int export_rm_bins(export_t * e);
 */

/** remove a new directory
 *
 * @param e: the export managing the file
 * @param pfid: the id of the parent
 * @param name: the name of directory to remove.
 * @param fid: fid_t of the removed directory to fill (used by upper level functions)
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_rmdir(export_t *e, fid_t pfid, char *name, fid_t fid);

/** create a symlink
 *
 * @param e: the export managing the file
 * @param link: target name
 * @param pfid: the id of the parent
 * @param name: the name of the file to link.
 * @param attrs: mattr_t to fill (used by upper level functions)
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_symlink(export_t *e, char *link, fid_t pfid, char *name,
        mattr_t * attrs);

/** create a symlink
 *
 * @param e: the export managing the file
 * @param fid: file id
 * @param link: link to fill
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_readlink(export_t *e, fid_t fid, char link[PATH_MAX]);

/** rename (move) a file
 *
 * @param e: the export managing the file
 * @param pfid: parent file id
 * @param name: file name
 * @param npfid: target parent file id
 * @param newname: target file name
 * @param fid: file id
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_rename(export_t * e, fid_t pfid, char *name, fid_t npfid,
        char *newname, fid_t fid);

/** Read to a regular file
 *
 * no effective read is done, just check if offset and len are consistent
 * with the file size, detect EOF, calculate index of the first block to read,
 * the nb. of bocks to read and update atime for this file
 *
 * @param e: the export managing the file
 * @param fid: id of the file to read
 * @param offset: offset to read from
 * @param len: length wanted
 * @param first_blk: id of the first block to read
 * @param nb_blks: Nb. of blocks to read
 *
 * @return: the readable length on success or -1 otherwise (errno is set)
 */
int64_t export_read(export_t * e, fid_t fid, uint64_t offset, uint32_t len, uint64_t * first_blk, uint32_t * nb_blks);

/** Get distributions for n blocks
 *
 * @param *e: the export managing the file
 * @param fid: id of the file to read
 * @param bid: first block address (from the start of the file)
 * @param n: number of blocks wanted
 * @param *d: pointer to distributions 
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_read_block(export_t *e, fid_t fid, bid_t bid, uint32_t n, dist_t * d);

/** Set distribution for n blocks and update the file size, mtime and ctime
 *
 * dist is the same for all blocks
 *
 * @param e: the export managing the file
 * @param fid: id of the file to read
 * @param bid: first block address (from the start of the file)
 * @param n: number of blocks
 * @param d: distribution to set 
 * @param off: offset to write from
 * @param len: length written
 *
 * @return: the written length on success or -1 otherwise (errno is set)
 */
int64_t export_write_block(export_t *e, fid_t fid, uint64_t bid, uint32_t n, dist_t d, uint64_t off, uint32_t len);

/** read a directory
 *
 * @param e: the export managing the file
 * @param fid: the id of the directory
 * @param children: pointer to pointer where the first children we will stored
 * @param cookie: index mdirentries where we must begin to list the mdirentries
 * @param eof: pointer that indicates if we list all the entries or not
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_readdir(export_t * e, fid_t fid, uint64_t * cookie, child_t **children, uint8_t *eof);

#endif
