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

#include "log.h"
#include "rozofs.h"
#include "list.h"
#include "mdir.h"

/**
 *  Mdirents structure: the mdirents file is a file that is associated with a directory
 *  it contains information related to childs of the directory: directory, regular file
 *  and symlink
 * The size of a mdirents file is fixed: 4K, and thus might contain up to 14 entries.
 * There are 2 type of mdirents file:
 * - MDIRENTS_FILE_ROOT_TYPE : 
 *  this is the root dirent file whose name corresponds to an index 
 *  obtained from a hash function perform on the name of an entry (dir. or file)
 * - MDIRENTS_FILE_COLL_TYPE:
 *  this is a collision mdirents file that can be either reference from 
 *  the root mdirents file or from another collision mdirent file.
 *       
 *   The structure of a mdirents file is the same whatever its type is.
 */

#define MDIRENTS_FILE_ROOT_TYPE 0  ///< root dirents file
#define MDIRENTS_FILE_COLL_TYPE 1  ///< coll dirents file
#define MAX_LV3_BUCKETS 4096  ///< coll dirents file

/**
 *  structure that define the type of a mdirents file used as linked list
 */
typedef struct _mdirents_file_link_t {
    uint8_t link_type; ///< either MDIRENTS_FILE_ROOT_TYPE or MDIRENTS_FILE_COLL_TYPE

    union {
        uint32_t hash_name_idx; ///< for MDIRENTS_FILE_ROOT_TYPE case
        fid_t fid; ///<  for MDIRENTS_FILE_COLL_TYPE case
    } link_ref;
} mdirents_file_link_t;

/**
 * mdirents file header structure: same structure on disk and memory
 */
typedef struct _mdirents_header_t {
    uint32_t mdirents_type; /// type of the mdirents file: ROOT or COLL
    uint32_t bitmap; ///< bitmap of the free entries in the file (max is 32)
    mdirents_file_link_t fid_cur; ///< unique file ID of the current mdirents file
    mdirents_file_link_t fid_next; ///< unique file ID of the next mdirents file (coll) or 0 if none
    mdirents_file_link_t fid_prev; ///< unique file ID of the previous mdirents file (coll) or 0 if none
    //list_t               list;         ///< linked list structure used when entry is loaded in memory
} mdirents_header_t;

/**
 * structure of a mdirent entry
 */
typedef struct _mdirents_entry_t {
    uint32_t type; ///< type of the entry: directory, regular file, symlink, etc... 
    fid_t fid; ///< unique ID allocated to the file or directory
    char name[ROZOFS_FILENAME_MAX]; ///< name of the directory or file
} mdirents_entry_t;


#define MDIRENTS_FILE_MAX_ENTRY  14  ///< max number of entries in a mdirents file
#define FULL_MDIRENTS_BITMAP  0x3fff  ///< Full bitmap in a mdirents file

/**
 *  structure of a mdirents file: type is a fixed size
 */
typedef struct _mdirents_file_t {
    mdirents_header_t header; ///< header of the dirent file: mainly management information
    mdirents_entry_t mdirentry[MDIRENTS_FILE_MAX_ENTRY]; ///< file/directory entries
} mdirents_file_t;

/**
 * API for put a mdirentry in one parent directory
 *
 * @param mdir: pointer to the mdirent structure for directory specific attributes
 * @param *name: pointer to the name of the mdirentry to put
 * @param fid: unique identifier of the mdirentry to put
 * @param type: type of the mdirentry to put
 *
 * @retval  0 on success
 * @retval -1 on failure
 */
int put_mdirentry(mdir_t * mdir, char * name, fid_t fid, uint32_t type);

/**
 * API for get a mdirentry in one parent directory
 *
 * @param mdir: pointer to the mdirent structure for directory specific attributes
 * @param name: (key) pointer to the name to search
 * @param *fid: pointer to the unique identifier for this mdirentry
 * @param *type: pointer to the type for this mdirentry
 *
 * @retval  0 on success
 * @retval -1 on failure
 */
int get_mdirentry(mdir_t * mdir, char * name, fid_t fid, uint32_t * type);

/**
 * API for delete a mdirentry in one parent directory
 * 
 * @param mdir: pointer to the mdirent structure for directory specific attributes
 * @param name: (key) pointer to the name of mdirentry to delete
 * @param *fid: pointer to the unique identifier fo this mdirentry
 * @param *type: pointer to the type for this mdirentry
 * 
 * @retval  0 on success
 * @retval -1 on failure
 */
int del_mdirentry(mdir_t * mdir, char * name, fid_t fid, uint32_t * type);

/**
 * API for list mdirentries of one directory
 * 
 * @param mdir: pointer to the mdirent structure for directory specific attributes
 * @param children: pointer to pointer where the first children we will stored
 * @param cookie: index mdirentries where we must begin to list the mdirentries
 * @param *eof: pointer that indicates if we list all the entries or not
 * 
 * @retval  0 on success
 * @retval -1 on failure
 */
int list_mdirentries(mdir_t * mdir, child_t ** children, uint64_t cookie, uint8_t * eof);
