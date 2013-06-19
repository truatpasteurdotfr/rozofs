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
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <pthread.h>

#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/epproto.h>
#include <rozofs/rpc/eproto.h>

#include "expgw_export.h"
#include "expgw_fid_cache.h"
#include "expgw_attr_cache.h"
#include "export.h"
#include "volume.h"
#include "exportd.h"
#include <rozofs/core/expgw_common.h>

DECLARE_PROFILING(epp_profiler_t);


void expgw_getattr_cbk(void *this,void *buffer) ;

/*
**______________________________________________________________________________
*/
/**
*  Export getattr

  That API attempts to find out the fid associated with a parent fid and a name
*/
void expgw_getattr_1_svc(epgw_mfile_arg_t * arg, expgw_ctx_t *req_ctx_p) 
{
    static epgw_mattr_ret_t ret;
    expgw_attr_cache_t *cache_attr_entry_p;
    int   export_lbg_id;
    int status;
    int local;

    ret.parent_attr.status = EP_EMPTY;        
    /**
    *  Get the lbg_id associated with the exportd
    */
    export_lbg_id = expgw_get_exportd_lbg(arg->arg_gw.eid);
    if (export_lbg_id < 0)
    {
      expgw_reply_error_no_such_eid(req_ctx_p,arg->arg_gw.eid);
      expgw_release_context(req_ctx_p);
      return;
    }
    /*
    ** check if the fid is handled by the current export gateway
    ** if the fid is not handled by it must operates in passthrough mode
    ** and must not update the cache
    */
    local = expgw_check_local(arg->arg_gw.eid,(unsigned char *)arg->arg_gw.fid);
    if (local != 0)
    {
       /*
       ** the export gateway must operate in passthrough mode
       */
       status = expgw_routing_rq_common(req_ctx_p,arg->arg_gw.eid,(unsigned char *)arg->arg_gw.fid,0,0,expgw_getattr_cbk,req_ctx_p);
       if (status < 0)
       {
         goto error;
       }
       goto out;
    }
    /*
    ** OK, the fid is handled by this server, so attempt to get the child fid
    ** from the cache
    ** do the lookup
    */
    cache_attr_entry_p = com_cache_bucket_search_entry(expgw_attr_cache_p,(unsigned char *)arg->arg_gw.fid);
    if (cache_attr_entry_p == NULL)
    {    
      /*
      ** entry has not be found in the cache, needs to interrogate the exportd
      */
      status = expgw_forward_rq_common(req_ctx_p,export_lbg_id,0,0,expgw_getattr_cbk,req_ctx_p);
      if (status < 0)
      {
        goto error;
      }
      goto out;
    }

    /*
    ** entry has been found, copy the attributes in the response buffer
    */
    memcpy(&ret.status_gw.ep_mattr_ret_t_u.attrs, &cache_attr_entry_p->attr, sizeof (mattr_t));
    
    ret.status_gw.status = EP_SUCCESS;
    /*
    ** use the receive buffer for the reply
    */
    req_ctx_p->xmitBuf = req_ctx_p->recv_buf;
    req_ctx_p->recv_buf = NULL;
    expgw_forward_reply(req_ctx_p,(char*)&ret);
    /*
    ** release the context
    */
    expgw_release_context(req_ctx_p);
    goto out;        

    
error:
    expgw_reply_error(req_ctx_p,errno);
    /*
    ** release the context
    */
    expgw_release_context(req_ctx_p);
out:
    return;
}

/*
**______________________________________________________________________________
*/
/**
*  Call back function call upon a success rpc, timeout or any other rpc failure
   The decoded data are found in the argument array of the export gateway context
   
   It is up to the function to release the received buffer. It might be
   possible that there is no buffer (typically in case of timeout)
*
 @param this : pointer to the gateway context
 @param param: pointer to the received buffer that contains the rpc response
 
 @return none
 */
void expgw_getattr_cbk(void *this,void *buffer) 
{
   epgw_mattr_ret_t *ret ;
   expgw_ctx_t *req_ctx_p = (expgw_ctx_t*) this;
   int local;
   
   ret = req_ctx_p->decoded_arg;
   
   req_ctx_p->xmitBuf = buffer;
   if (ret->status_gw.status == EP_FAILURE)
   {
     /*
     ** nothing more to do, just forward the received buffer to the
     ** source (rozofsmount)
     */
     goto error;
   }

   /*
   ** check the case of the cache attributes
   */
   local =  expgw_check_local(ret->hdr.eid,(unsigned char *)ret->status_gw.ep_mattr_ret_t_u.attrs.fid);
   while (local==0)
   {
      expgw_attr_cache_t *cache_attr_entry_p = NULL;
      com_cache_entry_t  *cache_entry;
      /*
      ** do the lookup on the attributes cache
      */
      cache_attr_entry_p = com_cache_bucket_search_entry(expgw_attr_cache_p,ret->status_gw.ep_mattr_ret_t_u.attrs.fid);
      if (cache_attr_entry_p != NULL)
      {
        /*
        ** entry has been found, copy the attributes in the response buffer
        */
        memcpy(&cache_attr_entry_p->attr, &ret->status_gw.ep_mattr_ret_t_u.attrs, sizeof (mattr_t));
        break;
      }
      cache_entry = expgw_attr_alloc_entry((mattr_t*)&ret->status_gw.ep_mattr_ret_t_u.attrs);  
      if (cache_entry == NULL)
      {        
        break;
      }
      if (com_cache_bucket_insert_entry(expgw_attr_cache_p, cache_entry) < 0)
      {
        severe("error on fid insertion"); 
        expgw_attr_release_entry(cache_attr_entry_p);
      }
      cache_attr_entry_p = (expgw_attr_cache_t*)cache_entry->usr_entry_p;
      break;
   }
   /*
   ** forward the response to the rozofsmount
   */
   expgw_common_reply_forward(req_ctx_p);
   goto out;
   
error:
    if (req_ctx_p->xmitBuf != NULL)
    {
      /*
      ** we call the same API as success case since the information 
      ** are already encoded in the received buffer (xmitBuf)
      */
      expgw_common_reply_forward(req_ctx_p);
      goto out;
    }
    req_ctx_p->xmitBuf = ruc_buf_getBuffer(EXPGW_NORTH_LARGE_POOL);
    if (req_ctx_p->xmitBuf != NULL)
    {
      expgw_reply_error(req_ctx_p,ret->status_gw.ep_mattr_ret_t_u.error);    
    }
    
out:
    /*
    ** release the gateway context
    */   
    expgw_release_context(req_ctx_p);
}




/*
**______________________________________________________________________________
*/
/**
*  Export setattr

  That request is just forwarded to the export without impacting the cache
*/
void expgw_setattr_1_svc(epgw_mfile_arg_t * arg, expgw_ctx_t *req_ctx_p) 
{
    int   export_lbg_id;
    int   lbg_id;
    int status;
    int local;
    
    /**
    *  Get the lbg_id associated with the exportd
    */
    export_lbg_id = expgw_get_exportd_lbg(arg->arg_gw.eid);
    if (export_lbg_id < 0)
    {
      expgw_reply_error_no_such_eid(req_ctx_p,arg->arg_gw.eid);
      expgw_release_context(req_ctx_p);
      return;
    }
    /*
    ** check if the fid is handled by the current export gateway
    ** if the fid is not handled by it must operate in passthrough mode
    ** 
    */
    local = expgw_check_local(arg->arg_gw.eid,(unsigned char *)arg->arg_gw.fid);
    if (local != 0)
    {
       lbg_id = expgw_get_export_gateway_lbg(arg->arg_gw.eid,(unsigned char *)arg->arg_gw.fid);
       if (lbg_id < 0)
       {
         errno = EINVAL;
         goto error;          
       }
       /*
       ** the export gateway must operate in passthrough mode
       */
       status = expgw_routing_rq_common(req_ctx_p,arg->arg_gw.eid,(unsigned char *)arg->arg_gw.fid,0,0,expgw_getattr_cbk,req_ctx_p);
       if (status < 0)
       {
         goto error;
       }
       goto out;
    }
    /*
    ** OK, the fid is handled by this server, in that case the request is forwarded
    ** to the master exportd
    */
    status = expgw_forward_rq_common(req_ctx_p,export_lbg_id,0,0,expgw_getattr_cbk,req_ctx_p);
    if (status < 0)
    {
      goto error;
    }
    goto out;        

    
error:
    expgw_reply_error(req_ctx_p,errno);
    /*
    ** release the context
    */
    expgw_release_context(req_ctx_p);
out:
    return;
}




/*
**______________________________________________________________________________
*/
/**
*   exportd write_block: update the size and date of a file
   
    note : the attributes are returned by the expgw_getattr_cbk() callback

    @param args : fid of the file, offset and length written
    
    @retval: EP_SUCCESS :attributes of the updated file
    @retval: EP_FAILURE :error code associated with the operation (errno)
*/
void expgw_write_block_1_svc(epgw_write_block_arg_t * arg, expgw_ctx_t *req_ctx_p) 
{
    int   export_lbg_id;
    int status;
    int local;
    expgw_attr_cache_t *cache_attr_entry_p;
    
    /**
    *  Get the lbg_id associated with the exportd
    */
    export_lbg_id = expgw_get_exportd_lbg(arg->arg_gw.eid);
    if (export_lbg_id < 0)
    {
      expgw_reply_error_no_such_eid(req_ctx_p,arg->arg_gw.eid);
      expgw_release_context(req_ctx_p);
      return;
    }
    /*
    ** check if the fid is handled by the current export gateway
    ** if the fid is not handled by it must operate in passthrough mode
    ** 
    */
    local = expgw_check_local(arg->arg_gw.eid,(unsigned char *)arg->arg_gw.fid);
    if (local != 0)
    {
       /*
       ** the export gateway must operate in passthrough mode
       */
       status = expgw_routing_rq_common(req_ctx_p,arg->arg_gw.eid,(unsigned char *)arg->arg_gw.fid,0,0,expgw_getattr_cbk,req_ctx_p);
       if (status < 0)
       {
         goto error;
       }
       goto out;
    }

    /*
    ** OK, the fid is handled by this server, attempt a lookup in the cache
    */
    cache_attr_entry_p = com_cache_bucket_search_entry(expgw_attr_cache_p,(unsigned char *)arg->arg_gw.fid);
    if (cache_attr_entry_p != NULL)
    {    
      if (cache_attr_entry_p->attr.size < (arg->arg_gw.offset+arg->arg_gw.length)) 
      {
        cache_attr_entry_p->attr.size = arg->arg_gw.offset+arg->arg_gw.length;
      }
    }  	


    /*
    ** OK, the fid is handled by this server, in that case the request is forwarded
    ** to the master exportd
    */
    status = expgw_forward_rq_common(req_ctx_p,export_lbg_id,0,0,expgw_getattr_cbk,req_ctx_p);
    if (status < 0)
    {
      goto error;
    }
    goto out;        

    
error:
    expgw_reply_error(req_ctx_p,errno);
    /*
    ** release the context
    */
    expgw_release_context(req_ctx_p);
out:
    return;
}
