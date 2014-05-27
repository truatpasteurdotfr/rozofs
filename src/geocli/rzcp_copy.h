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

#include <rozofs/rozofs.h>
#include <rozofs/rozofs_timer_conf.h>
#include <rozofs/core/ruc_list.h>
#include <rozofs/core/rozofs_tx_common.h>
#include <rozofs/core/rozofs_tx_api.h>
#include <rozofs/rpc/storcli_proto.h>
#include "rozofs_rw_load_balancing.h"
#include "rzcp_file_ctx.h"

#ifndef RZCP_COPY_H
#define RZCP_COPY_H

/**
* Align off as well as len to read on blocksize bundary
  @param[in]  off         : offset to read
  @param[in]  len         : length to read
  @param[out] off_aligned : aligned read offset
  @param[out] len_aligned : aligned length to read  
  
  @retval none
*/
static inline void rzcp_align_off_and_len(uint64_t off, uint64_t len, uint64_t * off_aligned, uint64_t * len_aligned) {

  *off_aligned = (off/ROZOFS_BSIZE)*ROZOFS_BSIZE;       
  *len_aligned = len + (off-*off_aligned);
  if ((*len_aligned % ROZOFS_BSIZE) == 0) return;
  *len_aligned = ((*len_aligned/ROZOFS_BSIZE)+1)*ROZOFS_BSIZE;
}


/**
* API for creation a transaction towards an storcli process

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
 @param fuse_ctx_p : pointer to the fuse context
  @param storcli_idx      : identifier of the storcli
 @param fid: file identifier: needed for the storcli load balancing context
 
 @retval 0 on success;
 @retval -1 on error,, errno contains the cause
 */

int rozofs_storcli_send_common(exportclt_t * clt,uint32_t timeout_sec,uint32_t prog,uint32_t vers,
                              int opcode,xdrproc_t encode_fct,void *msg2encode_p,
                              sys_recv_pf_t recv_cbk,void *fuse_ctx_p,
			                  int storcli_idx,fid_t fid);


/*
**_________________________________________________________________________
*/
/** Reads the distributions on the export server,
 *  adjust the read buffer to read only whole data blocks
 *  and uses the function read_blocks to read data
 *
 * @param *f: pointer to the cpy_p structure
 * @param off: offset to read from
 * @param *buf: pointer where the data will be stored: buffer associated with the cpy_p_t structure
 * @param len: length to read: (correspond to the max buffer size defined in the exportd parameters
 * @param *last_block_size_p: pointer to store the size of the last block size
 *  read
 *
 * @return: the length read on success, -1 otherwise (errno is set)
 */
 
int rzcp_read_req(rzcp_copy_ctx_t * cpy_p) ;

/*
**__________________________________________________________________
*/
/** 
    send a write request towards storcli
    it is assumed that the following parameters have been set in the
    write part of the context:
    off_cur : write offset
    len_cur : current length to write
    
    The main context must have valid information for the following fields:
    rzcp_copy_cbk : call upon end of the write request.
    shared_buf_ref and shared_buf_idx : respectively the shared buffer that contains the data and its ref
    storcli_idx : index of the storcli
 
  @param *cpy_p: pointer to copy context
  
  @retval none
*/
 
int rzcp_write_req(rzcp_copy_ctx_t * cpy_p);
/*
**_________________________________________________________________________
*/
/** 
 *  delete a file 
 * @param *f: pointer to the cpy_p structure
 * @param off: offset to read from
 * @param *buf: pointer where the data will be stored: buffer associated with the cpy_p_t structure
 * @param len: length to read: (correspond to the max buffer size defined in the exportd parameters
 * @param *last_block_size_p: pointer to store the size of the last block size
 *  read
 *
 * @return: the length read on success, -1 otherwise (errno is set)
 */
 
int rzcp_remove_req(rzcp_copy_ctx_t * cpy_p) ;
/*
**___________________________________________________________________
*/
/**
*  init of a copy

   @param fid_s: source fid
   @param off_start : start offet
   @param len: length to copy
   @param layout_s : source layout
   @param cid_s : source cluster id
   @param sids_s : source storage distribution
   
   @param fid_d: destination fid
   @param layout_d : destination layout
   @param cid_d : destination cluster id
   @param sids_d : destination storage distribution

   @param opaque: parameter to provide with the callback
   @param fct_cbk: callback function to call upon end of copy 
   
   @retval <> NULL : pointer to the allocated copy context 
   @retval NULL : no context: see errno for details
*/
rzcp_copy_ctx_t *rzcp_copy_init(fid_t fid_s,cid_t cid_s,sid_t *sids_s,uint8_t layout_s,uint64_t off_start,uint64_t len,
                   fid_t fid_d,cid_t cid_d,sid_t *sids_d,uint8_t layout_d,
		   void *opaque,rzcp_cpy_pf_t rzcp_caller_cbk);

#endif
