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
#ifndef DIRENT_WRITEBACK_CACHE_H
#define DIRENT_WRITEBACK_CACHE_H
#define _XOPEN_SOURCE 500

#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>

#include "mdir.h"
#include "mdirent.h"

#define DIRENT_CACHE_MAX_ENTRY   32
#define DIRENT_CACHE_MAX_CHUNK   16

typedef struct _dirent_chunk_cache_t
{
    off_t  off; /**< offset of the chunk  */
    char   *chunk_p; /**< pointer to the chunk array  */
}  dirent_chunk_cache_t;

/**
*  write back cache entry
*/
typedef struct _dirent_writeback_entry_t
{
     int fd;   /**< current fd: used as a key during the write  */
     int dir_fd; /**< file descriptor of the directory          */
     int state;  /**< O free: 1 busy*/
     /*
     ** identifier of the dirent file
     */
     fid_t     dir_fid;  /**< fid of the directory     */
     uint16_t   eid:8;  /**< reference of the export  */
     uint16_t   level_idx:8;
     uint16_t dirent_idx[MDIRENTS_MAX_LEVEL];
     /*
     **  pointer to the header of the dirent file (mdirents_file_t)
     */
     char *dirent_header;
     dirent_chunk_cache_t  chunk[DIRENT_CACHE_MAX_CHUNK];
} dirent_writeback_entry_t;


/*
** pointer to the dirent write back cache
*/
extern dirent_writeback_entry_t   *dirent_writeback_cache_p ;
/**
*____________________________________________________________
*/
/**
*  create cache entry
   
  @param eid : export identifier
  @param dirent_hdr_p : pointer to the dirent file header
  @param dir_fid : fid of the directory
  @param fd : file descriptor 

*/
int dirent_wbcache_open(int eid,mdirents_header_new_t *dirent_hdr_p,fid_t dir_fid);


/**
*____________________________________________________________
*/
/**
*  write the in the cache

   @param fd : file descriptor
   @param buf: buffer to write
   @param count : length to write
   @param offset: offset within the file
   
   @retval >= 0 : number of bytes written in the writeback cache
   @retval < 0 : error see errno for details
*/
int dirent_wbcache_write(int fd,void *buf,size_t count,off_t offset);

/**
*____________________________________________________________
*/
/**
*  close a file associated with a dirent writeback cache entry

   @param fd : file descriptor
   
   @retval >= 0 : number of bytes written in the writeback cache
   @retval < 0 : error see errno for details
*/
int dirent_wbcache_close(int fd);

/**
*____________________________________________________________
*/
/**
* init of the write back cache

  @retval 0 on success
  @retval < 0 error (see errno for details)
*/
int dirent_wbcache_init();

#endif
