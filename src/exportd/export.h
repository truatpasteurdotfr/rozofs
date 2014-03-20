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

#include <rozofs/rozofs.h>
#include <rozofs/common/dist.h>

#include "volume.h"
#include "cache.h"
#include "export_expgw_conf.h"

#define TRASH_DNAME "trash"
#define FSTAT_FNAME "fstat"
#define CONST_FNAME "const"

/** 'to_set' flags in setattr */
#define EXPORT_SET_ATTR_MODE  (1 << 0)
#define EXPORT_SET_ATTR_UID   (1 << 1)
#define EXPORT_SET_ATTR_GID   (1 << 2)
#define EXPORT_SET_ATTR_SIZE  (1 << 3)
#define EXPORT_SET_ATTR_ATIME (1 << 4)
#define EXPORT_SET_ATTR_MTIME (1 << 5)

/** Variables specific to the removal of the bins files */
/** Max nb. of subdirectories under trash directory (nb. of buckets) */
#define RM_MAX_BUCKETS 1024
/** Nb. max of entries to delete during one call of export_rm_bins function */
#define RM_FILES_MAX 500
/* Frequency calls of export_rm_bins function */
#define RM_BINS_PTHREAD_FREQUENCY_SEC 2
/** File size treshold: if a removed file is bigger than or equel to this size
 *  it will be push in front of list */
#define RM_FILE_SIZE_TRESHOLD 0x40000000LL



/** stat of an export
 * these values are independent of volume
 */
typedef struct export_fstat {
    uint64_t blocks;
    uint64_t files;
} export_fstat_t;

/** structure for store the list of files to remove
 * for one trash bucket.
 */
typedef struct trash_bucket {
    list_t rmfiles; ///< List of files to delete
    pthread_rwlock_t rm_lock; ///< Lock for the list of files to delete
} trash_bucket_t;




/**
* exportd gateway entry structure
*/

typedef struct expgw_entry {
    expgw_t expgw;
    list_t list;
} expgw_entry_t;

/** export stucture
 *
 */
typedef struct export {
    eid_t eid; ///< export identifier
    volume_t *volume; ///< the volume export relies on
    char root[PATH_MAX]; ///< absolute path of the storage root
    char md5[ROZOFS_MD5_SIZE]; ///< passwd
    uint8_t layout; ///< layout
    uint64_t squota; ///< soft quota in blocks
    uint64_t hquota; ///< hard quota in blocks
    export_fstat_t fstat; ///< fstat value
    int fdstat; ///< open file descriptor on stat file
    fid_t rfid; ///< root fid
    lv2_cache_t *lv2_cache; ///< cache of lv2 entries
    trash_bucket_t trash_buckets[RM_MAX_BUCKETS]; ///< table for store the list
    // of files to delete for each bucket trash
    pthread_t load_trash_thread; ///< pthread for load the list of trash files
    // to delete when we start or reload this export
} export_t;

extern uint32_t export_configuration_file_hash;  /**< hash value of the configuration file */


/** Remove bins files from trash 
 *
 * @param export: pointer to the export
 * @param first_bucket_idx: pointer for the first index bucket to remove
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_rm_bins(export_t * e, uint16_t * first_bucket_idx);


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
int export_initialize(export_t * e, volume_t *volume, 
        lv2_cache_t *lv2_cache, eid_t eid, const char *root, const char *md5,
        uint64_t squota, uint64_t hquota);

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
 * @param[out] attrs: mattr_t to fill (child attributes used by upper level functions)
 * @param[out] pattrs: mattr_t to fill (parent attributes) 
 
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_lookup(export_t *e, fid_t pfid, char *name, mattr_t * attrs, mattr_t * pattrs);

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
 * @param[out] attrs: mattr_t to fill (child attributes used by upper level functions)
 * @param[out] pattrs: mattr_t to fill (parent attributes)
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_link(export_t *e, fid_t inode, fid_t newparent, char *newname,
        mattr_t *attrs,mattr_t *pattrs);

/** create a new file
 *
 * @param e: the export managing the file
 * @param pfid: the id of the parent
 * @param name: the name of this file.
 * @param uid: the user id
 * @param gid: the group id
 * @param mode: mode of this file
 * @param[out] attrs: mattr_t to fill (child attributes used by upper level functions)
 * @param[out] pattrs: mattr_t to fill (parent attributes)
  
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_mknod(export_t *e, fid_t pfid, char *name, uint32_t uid,
        uint32_t gid, mode_t mode, mattr_t *attrs,mattr_t *pattrs);

/** create a new directory
 *
 * @param e: the export managing the file
 * @param pfid: the id of the parent
 * @param name: the name of this file.
 * @param uid: the user id
 * @param gid: the group id
 * @param mode: mode of this file
 * @param[out] attrs: mattr_t to fill (child attributes used by upper level functions)
 * @param[out] pattrs: mattr_t to fill (parent attributes)
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_mkdir(export_t *e, fid_t pfid, char *name, uint32_t uid,
        uint32_t gid, mode_t mode, mattr_t *attrs,mattr_t *pattrs);

/** remove a file
 *
 * @param e: the export managing the file
 * @param pfid: the id of the parent
 * @param name: the name of this file.
 * @param[out] fid: the fid of the removed file
 * @param[out] pattrs: mattr_t to fill (parent attributes)
 * 
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_unlink(export_t * e, fid_t pfid, char *name, fid_t fid, mattr_t * pattrs);

/*
int export_rm_bins(export_t * e);
 */

/** remove a new directory
 *
 * @param e: the export managing the file
 * @param pfid: the id of the parent
 * @param name: the name of directory to remove.
 * @param[out] fid: the fid of the removed file
 * @param[out] pattrs: mattr_t to fill (parent attributes)
 * 
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_rmdir(export_t *e, fid_t pfid, char *name, fid_t fid, mattr_t * pattrs);

/** create a symlink
 *
 * @param e: the export managing the file
 * @param link: target name
 * @param pfid: the id of the parent
 * @param name: the name of the file to link.
 * @param[out] attrs: mattr_t to fill (child attributes used by upper level functions)
 * @param[out] pattrs: mattr_t to fill (parent attributes)
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_symlink(export_t *e, char *link, fid_t pfid, char *name,
        mattr_t * attrs,mattr_t * pattrs);

/** read a symbolic link
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
        char *newname, fid_t fid,
	mattr_t * attrs);

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

/** update the file size, mtime and ctime
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
 * @param[out] attrs: updated attributes of the file
 *
 * @return: the written length on success or -1 otherwise (errno is set)
 */
int64_t export_write_block(export_t *e, fid_t fid, uint64_t bid, uint32_t n, dist_t d, uint64_t off, uint32_t len,mattr_t *attrs);

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

/** retrieve an extended attribute value.
 *
 * @param e: the export managing the file or directory.
 * @param fid: the id of the file or directory.
 * @param name: the extended attribute name.
 * @param value: the value of this extended attribute.
 * @param size: the size of a buffer to hold the value associated
 *  with this extended attribute.
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
ssize_t export_getxattr(export_t *e, fid_t fid, const char *name, void *value, size_t size);

/** set an extended attribute value for a file or directory.
 *
 * @param e: the export managing the file or directory.
 * @param fid: the id of the file or directory.
 * @param name: the extended attribute name.
 * @param value: the value of this extended attribute.
 * @param size: the size of a buffer to hold the value associated
 *  with this extended attribute.
 * @param flags: parameter can be used to refine the semantics of the operation.
 * 
 * @return: On success, zero is returned.  On failure, -1 is returned.
 */
int export_setxattr(export_t *e, fid_t fid, char *name, const void *value, size_t size, int flags);

/** remove an extended attribute from a file or directory.
 *
 * @param e: the export managing the file or directory.
 * @param fid: the id of the file or directory.
 * @param name: the extended attribute name.
 * 
 * @return: On success, zero is returned.  On failure, -1 is returned.
 */
int export_removexattr(export_t *e, fid_t fid, char *name);

/** list extended attribute names from the lv2 regular file.
 *
 * @param e: the export managing the file or directory.
 * @param fid: the id of the file or directory.
 * @param list: list of extended attribute names associated with this file/dir.
 * @param size: the size of a buffer to hold the list of extended attributes.
 * 
 * @return: On success, the size of the extended attribute name list.
 * On failure, -1 is returned and errno is set appropriately.
 */
ssize_t export_listxattr(export_t *e, fid_t fid, void *list, size_t size);

/*
 *_______________________________________________________________________
 */

/**
*  Non blocing thread section
*/
/*
 *_______________________________________________________________________
 */
#define EXPORTNB_CNF_NO_BUF_CNT          1
#define EXPORTNB_CNF_NO_BUF_SZ           1
/**
*  North Interface
*/
#define EXPORTNB_CTX_CNT   32  /**< context for processing either a read or write request from rozofsmount and internal read req */
#define EXPORTNB_CTX_MIN_CNT 3 /**< minimum count to process a request from rozofsmount */
/**
* Buffer s associated with the reception of the load balancing group on north interface
*/
#define EXPORTNB_NORTH_LBG_BUF_RECV_CNT   EXPORTNB_CTX_CNT  /**< number of reception buffer to receive from rozofsmount */
#define EXPORTNB_NORTH_LBG_BUF_RECV_SZ    (1024*38)  /**< max user data payload is 38K       */
/**
* Storcli buffer mangement configuration
*  INTERNAL_READ are small buffer used when a write request requires too trigger read of first and/or last block
*/
#define EXPORTNB_NORTH_MOD_INTERNAL_READ_BUF_CNT   EXPORTNB_CTX_CNT  /**< expgw_north_small_buf_count  */
#define EXPORTNB_NORTH_MOD_INTERNAL_READ_BUF_SZ   2048  /**< expgw_north_small_buf_sz  */

#define EXPORTNB_NORTH_MOD_XMIT_BUF_CNT   EXPORTNB_CTX_CNT  /**< expgw_north_large_buf_count  */
#define EXPORTNB_NORTH_MOD_XMIT_BUF_SZ    EXPORTNB_NORTH_LBG_BUF_RECV_SZ  /**< expgw_north_large_buf_sz  */

#define EXPORTNB_SOUTH_TX_XMIT_BUF_CNT   (EXPORTNB_CTX_CNT)  /**< expgw_south_large_buf_count  */
#define EXPORTNB_SOUTH_TX_XMIT_BUF_SZ    (1024*8)                           /**< expgw_south_large_buf_sz  */

/**
* configuartion of the resource of the transaction module
*
*  concerning the buffers, only reception buffer are required 
*/

#define EXPORTNB_SOUTH_TX_CNT   (EXPORTNB_CTX_CNT)  /**< number of transactions towards storaged  */
#define EXPORTNB_SOUTH_TX_RECV_BUF_CNT   EXPORTNB_SOUTH_TX_CNT  /**< number of recption buffers  */
#define EXPORTNB_SOUTH_TX_RECV_BUF_SZ    EXPORTNB_SOUTH_TX_XMIT_BUF_SZ  /**< buffer size  */

#define EXPORTNB_NORTH_TX_BUF_CNT   0  /**< not needed for the case of storcli  */
#define EXPORTNB_NORTH_TX_BUF_SZ    0  /**< buffer size  */


#define EXPORTNB_MAX_RETRY   3  /**< max attempts for read or write on mstorage */

extern volatile int expgwc_non_blocking_thread_started; /**< clear on start, and asserted by non blocking thread when ready */

typedef struct _exportd_start_conf_param_t
{
   uint16_t debug_port;   /**< port value to be used by rmonitor  */
   uint16_t instance;     /**< rozofsmount instance: needed when more than 1 rozofsmount run the same server and exports the same filesystem */
   char     *exportd_hostname;  /**< hostname of the exportd: needed by nobody!!! */
} exportd_start_conf_param_t;

/*
 *_______________________________________________________________________
 */

/**
 *  This function is the entry point for setting rozofs in non-blocking mode

   @param args->ch: reference of the fuse channnel
   @param args->se: reference of the fuse session
   @param args->max_transactions: max number of transactions that can be handled in parallel
   
   @retval -1 on error
   @retval : no retval -> only on fatal error

 */
int expgwc_start_nb_blocking_th(void *args);

/**
*  Init of the channel module (interface between the main process and the non-blocking thread
*/
uint32_t expgwc_int_chan_moduleInit();

/*
 *_______________________________________________________________________
 */
/**
*  Init of the array that is used for building an exportd configuration message
  That array is allocated during the initialization of the exportd and might be
  released upon the termination of the exportd process
  
  @param none
  
  @retval 0 on success
  @retval -1 on error
 */
int exportd_init_storage_configuration_message();


/*
 *_______________________________________________________________________
 */
 /**
 *  That API is intended to be called by ep_conf_storage_1_svc() 
    prior to build the configuration message
    
    The goal is to clear the number of storages and to clear the
    number of sid per storage entry
    
    @param none
    retval none
*/
void exportd_reinit_storage_configuration_message();


/*
 *_______________________________________________________________________
 */
/**
*  Init of the array that is used for building an exportd configuration message
  That array is allocated during the initialization of the exportd and might be
  released upon the termination of the exportd process
  
  @param none
  
  @retval 0 on success
  @retval -1 on error
 */
int exportd_init_storage_configuration_message();

/*
**______________________________________________________________________________
*/
/**
*  Init of the data structure used for sending out export gateway configuration
   to rozofsmount.
   That API must be called during the inif of exportd
  
  @param exportd_hostname: VIP address of the exportd (extracted from the exportd configuration file)
  @retval none
*/
void ep_expgw_init_configuration_message(char *exportd_hostname);

/*
**______________________________________________________________________________
*/
/** Set a lock on a file
 *
 * @param e: the export managing the file or directory.
 * @param fid: the id of the file or directory.
 * @param lock: the lock to set/remove
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
int export_set_file_lock(export_t *e, fid_t fid, ep_lock_t * lock_requested, ep_lock_t * blocking_lock) ;
/*
**______________________________________________________________________________
*/
/** reset all locks from a client
 *
 * @param e: the export managing the file or directory.
 * @param lock: the identifier of the client whose locks are to remove
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
int export_clear_client_file_lock(export_t *e, ep_lock_t * lock_requested);
/*
**______________________________________________________________________________
*/
/** reset all locks from an owner
 *
 * @param e: the export managing the file or directory.
 * @param lock: the identifier of the client whose locks are to remove
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
int export_clear_owner_file_lock(export_t *e, fid_t fid, ep_lock_t * lock_requested);
/*
**______________________________________________________________________________
*/
/** Get a lock on a file
 *
 * @param e: the export managing the file or directory.
 * @param fid: the id of the file or directory.
 * @param lock: the lock to set/remove
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
int export_get_file_lock(export_t *e, fid_t fid, ep_lock_t * lock_requested, ep_lock_t * blocking_lock) ;
/*
**______________________________________________________________________________
*/
/** Get a poll event from a client
 *
 * @param e: the export managing the file or directory.
 * @param lock: the lock to set/remove
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
int export_poll_file_lock(export_t *e, ep_lock_t * lock_requested) ;
#endif
