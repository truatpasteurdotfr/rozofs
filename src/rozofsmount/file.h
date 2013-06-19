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

#ifndef _FILE_H
#define _FILE_H

#include <rozofs/rozofs.h>
#include <rozofs/rpc/eclient.h>
#include <rozofs/rpc/sclient.h>
#include <rozofs/core/ruc_list.h>


typedef enum 
{
  BUF_ST_EMPTY = 0,
  BUF_ST_READ_BEFORE,
  BUF_ST_READ_INSIDE,
  BUF_ST_READ_AFTER,
  BUF_ST_WRITE_BEFORE,
  BUF_ST_WRITE_INSIDE,
  BUF_ST_WRITE_AFTER,
  BUF_ST_MAX
} rozofs_buf_read_write_state_e;

 
 
 typedef enum 
 {
    BUF_ACT_COPY_EMPTY = 0,
    BUF_ACT_COPY,
    BUF_ACT_COPY_OVERLAP,
    BUF_ACT_FLUSH_THEN_COPY_NEW,
    BUF_ACT_FLUSH_ALIGN_THEN_COPY_NEW,
    BUF_ACT_MAX
 } rozofs_buf_read_write_action_e;


 
 typedef enum 
 {
    BUF_STATUS_DONE = 0,
    BUF_STATUS_WR_IN_PRG ,
    BUF_STATUS_FAILURE ,
    BUF_STATUS_MAX
 } rozofs_buf_read_write_status_e;


typedef struct _rozo_buf_rw_status_t {
    int status;
    int errcode;
} rozo_buf_rw_status_t;



typedef struct file {
    ruc_obj_desc_t pending_rd_list;  /**< used to queue the FUSE contextr for which a read is requested  */
    ruc_obj_desc_t pending_wr_list;  /**< used to queue the FUSE context waiting for flush completed  */
    fid_t fid;
    mode_t mode;
    mattr_t attrs;
    exportclt_t *export;
//    sclient_t **storages;
    //char buffer[ROZOFS_BUF_SIZE];
    char *buffer;
    int closing;             /**< assert to 1 when the file has to be closed and there are some disks operation pending */
    int buf_write_wait;
    int buf_write_pending;   /**< number of write requests that are pending */
    int buf_read_pending;    /**< number of read requests that are pending */
    int wr_error;            /**< last write error code                     */
    int buf_read_wait;
    uint64_t read_pos;  /**< absolute position of the first available byte to read*/
    uint64_t read_from; /**< absolute position of the last available byte to read */
    uint64_t write_pos;  /**< absolute position of the first available byte to read*/
    uint64_t write_from; /**< absolute position of the last available byte to read */
#if 0
    char *buffer;
    int buf_write_wait;
    int buf_read_wait;
    uint64_t buf_pos;
    uint64_t buf_from;
#endif
} file_t;

/**
*  Init of the file structure 

 @param file : pointer to the structure
 
 @retval none
 */
static inline void rozofs_file_working_var_init(file_t *file)
{
    /*
    ** init of the variable used for buffer management
    */
    file->closing   = 0;
    file->wr_error  = 0;
    file->buf_write_pending = 0;
    file->buf_read_pending  = 0;
    file->read_from  = 0;
    file->read_pos   = 0;
    file->write_from = 0;
    file->write_pos  = 0;
    file->buf_write_wait = 0;
    file->buf_read_wait = 0;    
    ruc_listHdrInit(&file->pending_rd_list);
    ruc_listHdrInit(&file->pending_wr_list);
}




/**
*  Close of file descriptor 

 @param file : pointer to the file decriptor structure
 
 @retval 1  on success
 @retval 0  if pending
 */

static inline int file_close(file_t * f) {

     f->closing = 1;
     /*
     ** Check if there some pending read or write
     */     
     if ((f->buf_write_pending) || (f->buf_read_pending))
     {
       /*
       ** need to wait for end of pending transaction
       */
       return 0;
     }
     /*
     ** Release all memory allocated
     */
     free(f->buffer);
     free(f);
    return 1;
}

/**
* API to return the closing state of a file descriptor

  @param file : file descriptor
  
 @retval 1  closing
 @retval 0  not closed
 */ 
 static inline int rozofs_is_file_closing (file_t * f) 
{
   return f->closing;
}
#endif
