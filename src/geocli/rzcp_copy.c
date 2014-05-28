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

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <rozofs/common/log.h>
#include <rozofs/core/ruc_list.h>
#include <rozofs/core/ruc_buffer_debug.h>
#include <rozofs/core/uma_dbg_api.h>
#include "rzcp_file_ctx.h"
#include <rozofs/core/rozofs_tx_api.h>
#include "rozofs_rw_load_balancing.h"
#include "rozofs_sharedmem.h"


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
		   void *opaque,rzcp_cpy_pf_t rzcp_caller_cbk)
{
   rzcp_copy_ctx_t *cpy_p= NULL;
   rzcp_file_ctx_t *rw_ctx_p;
   /*
   ** allocate a fresh copy context
   */
   cpy_p = rzcp_alloc();
   if (cpy_p == NULL)
   {
      /*
      ** out of context
      */
      errno = ENOMEM;
      goto error;
   }
   /*
   ** allocate a shared buffer for read/write transaction with storcli
   ** note : we always use the source fid to determine the storcli to use
   */
#warning cpy_p->storcli_idx = 0
//   cpy_p->storcli_idx = stclbg_storcli_idx_from_fid(fid_s);
   cpy_p->storcli_idx = 0;
   uint32_t *p32;
   uint32_t length;
   int k;
   for (k= 0; k <SHAREMEM_PER_FSMOUNT ; k++)
   {
     cpy_p->shared_buf_ref[k] = rozofs_alloc_shared_storcli_buf(k);
     if (cpy_p->shared_buf_ref[k] == NULL)
     { 
	/*
	*  out of shared buffer
	*/
	errno = ENOMEM;
	goto error;
     }
     /*
     ** we got one buffer for storing read/write data:
     ** clear the first 4 bytes of the array that is supposed to contain
     ** the reference of the transaction
     */
     p32 = (uint32_t *)ruc_buf_getPayload(cpy_p->shared_buf_ref[k]);
     *p32 = 0;
     /*
     ** get the index of the shared payload in buffer
     */
     cpy_p->shared_buf_idx[k] = rozofs_get_shared_storcli_payload_idx(cpy_p->shared_buf_ref[k],k,&length);   
   }
   /*
   ** fill up the context
   */
   cpy_p->opaque          = opaque;
   cpy_p->rzcp_caller_cbk = rzcp_caller_cbk;
   /*
   ** source information
   */
   rw_ctx_p = (rzcp_file_ctx_t *)&cpy_p->read_ctx;
   memcpy(rw_ctx_p->fid,fid_s,sizeof(fid_t));
   rw_ctx_p->cid = cid_s;
   memcpy(rw_ctx_p->sids,sids_s,ROZOFS_SAFE_MAX*sizeof(sid_t));
   rw_ctx_p->layout = layout_s;
   rw_ctx_p->off_start = off_start;
   rw_ctx_p->initial_len = (int64_t)len;
   /*
   ** destination information
   */
   rw_ctx_p = (rzcp_file_ctx_t *)&cpy_p->write_ctx;
   memcpy(rw_ctx_p->fid,fid_d,sizeof(fid_t));
   rw_ctx_p->cid = cid_d;
   memcpy(rw_ctx_p->sids,sids_d,ROZOFS_SAFE_MAX*sizeof(sid_t));
   rw_ctx_p->layout = layout_d;   

   return cpy_p;
   
error:
   return NULL;
}
   
/*
**___________________________________________________________________
*/
