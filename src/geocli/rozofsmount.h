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

#include <rozofs/rpc/rozofs_rpc_util.h>
#include <rozofs/core/rozofs_tx_common.h>

#include "file.h"

#define hash_xor8(n)    (((n) ^ ((n)>>8) ^ ((n)>>16) ^ ((n)>>24)) & 0xff)
#define ROOT_INODE 1

extern exportclt_t exportclt;


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
    unsigned quota; /* ignored */
    unsigned noXattr;
    int site;
    int conf_site_file;
    unsigned running_site;
} rozofsmnt_conf_t;


typedef struct _rozofs_fuse_conf_t
{
   uint16_t debug_port;   /**< port value to be used by rmonitor  */
   uint16_t instance;     /**< rozofsmount instance: needed when more than 1 rozofsmount run the same server and exports the same filesystem */
   uint16_t nb_cores;     /**< Number of core files */
   void     *se;          /**< pointer to the session context     */
   void    *ch;           /**< pointer to the channel context     */
   void    *exportclt;           /**< pointer to the exportd conf     */
   int      max_transactions; /**< max number of simultaneous transactions */
   unsigned site;

} rozofs_fuse_conf_t;

extern int rozofs_site_number;
/**______________________________________________________________________________
*/
/**
*  get the current site number of the rozofsmount client

*/
static inline int rozofs_get_site_number()
{
  return rozofs_site_number;
}
/*
**____________________________________________________
*/
/**
* API for creation a transaction towards an exportd

 The reference of the north load balancing is extracted for the client structure
 fuse_ctx_p:
 That API needs the pointer to the current fuse context. That nformation will be
 saved in the transaction context as userParam. It is intended to be used later when
 the client gets the response from the server
 encoding function;
 For making that API generic, the caller is intended to provide the function that
 will encode the message in XDR format. The source message that is encoded is 
 supposed to be pointed by msg2encode_p.
 Since the service is non-blocking, the caller MUST provide the callback function 
 that will be used for decoding the message
 
 
 @param clt        : pointer to the client structure
 @param timeout_sec : transaction timeout
 @param prog       : program
 @param vers       : program version
 @param opcode     : metadata opcode
 @param encode_fct : encoding function
 @msg2encode_p     : pointer to the message to encode
 @param recv_cbk   : receive callback function
 @param sync_ctx_p : pointer to the synchro context
 
 @retval 0 on success;
 @retval -1 on error,, errno contains the cause
 */

int rozofs_export_send_common(exportclt_t * clt,uint32_t timeout_sec,uint32_t prog,uint32_t vers,
                              int opcode,xdrproc_t encode_fct,void *msg2encode_p,
                              sys_recv_pf_t recv_cbk,void *sync_ctx_p);
/*
**____________________________________________________
*/

int georep_lbg_initialize(exportclt_t *exportclt ,unsigned long prog,
        unsigned long vers,uint32_t port_num);
#endif
