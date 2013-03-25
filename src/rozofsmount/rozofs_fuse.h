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
 
#ifndef ROZOFS_FUSE_H
#define ROZOFS_FUSE_H
#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 26
#endif

#include <stdlib.h>
#include <sys/types.h> 
#include <fuse/fuse_lowlevel.h>
#include <fuse/fuse_opt.h>
#include "rozofsmount.h"

 /**
 * Must be the same as sys_recv_pf_t
 */
 typedef void (*fuse_end_tx_recv_pf_t)(void *tx_ctx,void *usr_param);

typedef struct _rozofs_fuse_conf_t
{
   uint16_t debug_port;   /**< port value to be used by rmonitor  */
   uint16_t instance;     /**< rozofsmount instance: needed when more than 1 rozofsmount run the same server and exports the same filesystem */
   void     *se;          /**< pointer to the session context     */
   void    *ch;           /**< pointer to the channel context     */
   void    *exportclt;           /**< pointer to the exportd conf     */
   int      max_transactions; /**< max number of simultaneous transactions */
} rozofs_fuse_conf_t;




/**
* fuse request context
*
*  Caution: need to double checked that the pointer are pointer
* either to the fuse message or a context allocated by fuse but not
* a pointer on a local variable
*/
typedef struct _rozofs_fuse_save_ctx_t
{
   ruc_obj_desc_t link;   /**< uwe to queue to context on the file_t structure */
   void  *buf_ref;        /**< pointer to the mabagement part of the buffer    */
   fuse_req_t req;  /**< fuse request  */
   fuse_ino_t ino;  /**< fuse inode input argument  */
   fuse_ino_t parent;
   fuse_ino_t newparent;
   char *newname;
   struct fuse_file_info *fi;
   char *name;
   mode_t mode;
   off_t off;
   size_t size;
   int to_set;
   dev_t rdev;
   uint64_t time;
   dirbuf_t db;
   fuse_end_tx_recv_pf_t proc_end_tx_cbk;   /**< callback that must be call at end of transaction (mainly used by write/flush and close */ 
   uint64_t buf_flush_offset;               /**< offset of the first byte to flush    */
   uint32_t buf_flush_len;               /**< length of the data flush to disk    */
   
 } rozofs_fuse_save_ctx_t;
 
 
  /**
* rozofs fuse data structure
*/
typedef struct _rozofs_fuse_ctx_t
{
   void *fuseReqPoolRef;     /**< reference of save fuse context pool     */
   uint32_t bufsize;          /**< size of the request buffer              */
   struct fuse_chan *ch;     /**< channel reference                       */
   struct fuse_session *se;  /**< fuse session                            */
   int    fd;                /**< /dev/fuse file descriptor               */
   void   *connectionId;     /**< socket controller reference             */
   int     congested;        /**< assert to 1 when the transmitter is congested  */
   char   *buf_fuse_req_p;   /**< fuse request buffer                      */

} rozofs_fuse_ctx_t;
 
 
 extern rozofs_fuse_ctx_t  *rozofs_fuse_ctx_p ;  /**< pointer to the rozofs_fuse saved contexts   */

/**
* Prototypes
*/
/*
**__________________________________________________________________________
*/
/**
*  Init of the pseudo fuse thread 

  @param ch : initial channel
  @param se : initial session
  @param rozofs_fuse_buffer_count : number of request buffers  
  
  @retval 0 on success
  @retval -1 on error
*/
int rozofs_fuse_init(struct fuse_chan *ch,struct fuse_session *se,
                     int rozofs_fuse_buffer_count);

/*_______________________________________________________________________
*/
/**
*  This function is the entry point for setting rozofs in non-blocking mode

   @param args->ch: reference of the fuse channnel
   @param args->se: reference of the fuse session
   @param args->max_transactions: max number of transactions that can be handled in parallel
   
   @retval -1 on error
   @retval : no retval -> only on fatal error

*/
int rozofs_stat_start(void *args);

 /*
 *________________________________________________________
 */
 /*
 ** API to be called for stopping rozofsmount
 
  @param none
  @retval none
*/
 void rozofs_exit();
 
#endif
