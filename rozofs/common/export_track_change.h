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
#ifndef EXPORT_TRACKING_CHANGE_H
#define EXPORT_TRACKING_CHANGE_H


#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define EXPT_BITMAP_SZ_BIT 10
#define EXPT_BITMAP_SZ_BYTE (1<<EXPT_BITMAP_SZ_BIT)

#define EXPT_MAX_BUFFER 16
#define EXPT_MAX_BUFFER_SZ ((MAX_SLICE_NB*EXPT_BITMAP_SZ_BYTE)/EXPT_MAX_BUFFER)
#define EXPT_MAX_NAME 1024


typedef struct _expt_ctx_buf_t
{
   uint64_t bitmap;
   char *bitmap_buf[EXPT_MAX_BUFFER];
} expt_ctx_buf_t;

typedef struct _expt_ctx_t
{
   pthread_rwlock_t lock;
   int  cur_buf;
   uint64_t request_count;
   char name[EXPT_MAX_NAME];
   time_t last_time_change;
   expt_ctx_buf_t buf[2];
} expt_ctx_t;


/*
**__________________________________________________________________
*/
/**
*  allocation a context for tracking inode change or update

   @param name: name to the tracking inode context
   @retval <>NULL pointer to the allocated context
   @retval NULL: out of memory
*/
expt_ctx_t *expt_alloc_context(char *name);
/*
**__________________________________________________________________
*/
/**
* assert the bit corresponding to inode on with there is a change
  This is done for the file that contains the inode
  
  @param slice: slice of the inode
  @param file_id: file id that contains the inode
  
  @retval 0 on success
  @retval -1 on error
*/
int expt_set_bit(expt_ctx_t *p,int slice,uint64_t file_id);

#endif
