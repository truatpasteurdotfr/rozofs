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


void expgw_delete_dir_file_cbk(void *this,void *buffer) ;
/*
**______________________________________________________________________________
*/
/**
*  Build the key to perform a lookup with a <fid,name>

  @param Pkey: pointer to the resulting key
  @param pfid: parent fid
  @param name: filename or directory name
 
  @retval none
*/
static inline void expgw_fid_build_key(expgw_fid_key_t *Pkey,unsigned char * pfid,char *name) 
{
  memcpy(Pkey->pfid,pfid,sizeof(fid_t));  
  Pkey->name_len = strlen(name);
  Pkey->name     = name;
}
/*
**______________________________________________________________________________
*/
/**
*  Export rmdir:

  remove the entry for the local fid cache and from the attributes cache if it is local
  to the exportd gateway.
 

  That API attempts to find out the fid associated with a parent fid and a name
  
*/
void expgw_rmdir_1_svc(epgw_rmdir_arg_t * arg, expgw_ctx_t *req_ctx_p) 
{
    static epgw_fid_ret_t ret;
    expgw_fid_key_t key;
    expgw_fid_cache_t  *cache_fid_entry_p;
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
    local = expgw_check_local(arg->arg_gw.eid,(unsigned char *)arg->arg_gw.pfid);
    if (local != 0)
    {
       /*
       ** the export gateway must operate in passthrough mode
       */
       status = expgw_routing_rq_common(req_ctx_p,arg->arg_gw.eid,(unsigned char *)arg->arg_gw.pfid,0,0,expgw_delete_dir_file_cbk,req_ctx_p);
       if (status < 0)
       {
         goto error;
       }
       goto out;
    }
    /*
    ** OK, the fid is handled by this server, so attempt to get the child fid
    ** from the cache
    */
    expgw_fid_build_key(&key,(unsigned char *)arg->arg_gw.pfid,arg->arg_gw.name);
    /*
    ** do the lookup
    */
    cache_fid_entry_p = com_cache_bucket_search_entry(expgw_fid_cache_p,&key);
    if (cache_fid_entry_p == NULL)
    {
      /*
      ** entry has not be found in the cache, ok forward the request to the exportd for doing the job
      ** of removing the entry
      */
      status = expgw_forward_rq_common(req_ctx_p,export_lbg_id,0,0,expgw_delete_dir_file_cbk,req_ctx_p);
      if (status < 0)
      {
        goto error;
      }
      goto out;
    }
    /*
    ** OK, there is a match, check if it is the same export gateway that handles the
    ** attributes: if there is a match on the attributes just remove the entry
    */    
    local = expgw_check_local(arg->arg_gw.eid,cache_fid_entry_p->fid);
    if (local == 0)
    {
      /*
      **attempt to remove it from the cache
      */
      com_cache_bucket_remove_entry(expgw_attr_cache_p,cache_fid_entry_p->fid);    
    }
    /*
    ** now remove the fid entry from the fid cache and then forwards the rmdir towards the exportd
    */
    com_cache_bucket_remove_entry(expgw_fid_cache_p,&key);
    /*
    ** forward the rmdir request towards the master exportd
    */
    status = expgw_forward_rq_common(req_ctx_p,export_lbg_id,0,0,expgw_delete_dir_file_cbk,req_ctx_p);
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
*  Export unlink:

  remove the entry for the local fid cache and from the attributes cache if it is local
  to the exportd gateway.
 

  That API attempts to find out the fid associated with a parent fid and a name
  
*/
void expgw_unlink_1_svc(epgw_unlink_arg_t * arg, expgw_ctx_t *req_ctx_p) 
{
    static epgw_fid_ret_t ret;
    expgw_fid_key_t key;
    expgw_fid_cache_t  *cache_fid_entry_p;
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
    local = expgw_check_local(arg->arg_gw.eid,(unsigned char *)arg->arg_gw.pfid);
    if (local != 0)
    {
       /*
       ** the export gateway must operate in passthrough mode
       */
       status = expgw_routing_rq_common(req_ctx_p,arg->arg_gw.eid,(unsigned char *)arg->arg_gw.pfid,0,0,expgw_delete_dir_file_cbk,req_ctx_p);
       if (status < 0)
       {
         goto error;
       }
       goto out;
    }
    /*
    ** OK, the fid is handled by this server, so attempt to get the child fid
    ** from the cache
    */
    expgw_fid_build_key(&key,(unsigned char *)arg->arg_gw.pfid,arg->arg_gw.name);
    /*
    ** do the lookup
    */
    cache_fid_entry_p = com_cache_bucket_search_entry(expgw_fid_cache_p,&key);
    if (cache_fid_entry_p == NULL)
    {
      /*
      ** entry has not be found in the cache, ok forward the request to the exportd for doing the job
      ** of removing the entry
      */
      status = expgw_forward_rq_common(req_ctx_p,export_lbg_id,0,0,expgw_delete_dir_file_cbk,req_ctx_p);
      if (status < 0)
      {
        goto error;
      }
      goto out;
    }
    /*
    ** OK, there is a match, check if it is the same export gateway that handles the
    ** attributes: if there is a match on the attributes just remove the entry
    */    
    local = expgw_check_local(arg->arg_gw.eid,cache_fid_entry_p->fid);
    if (local == 0)
    {
      /*
      **attempt to remove it from the cache
      */
      com_cache_bucket_remove_entry(expgw_attr_cache_p,cache_fid_entry_p->fid);    
    }
    /*
    ** now remove the fid entry from the fid cache and then forwards the rmdir towards the exportd
    */
    com_cache_bucket_remove_entry(expgw_fid_cache_p,&key);
    /*
    ** forward the rmdir request towards the master exportd
    */
    status = expgw_forward_rq_common(req_ctx_p,export_lbg_id,0,0,expgw_delete_dir_file_cbk,req_ctx_p);
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
*  Call back function call upon a success rpc, timeout or any other rpc failure
   The decoded data are found in the argument array of the export gateway context
   
   It is up to the function to release the received buffer. It might be
   possible that there is no buffer (typically in case of timeout)
*
 @param this : pointer to the gateway context
 @param param: pointer to the received buffer that contains the rpc response
 
 @return none
 */
void expgw_delete_dir_file_cbk(void *this,void *buffer) 
{
   epgw_fid_ret_t *ret ;
   expgw_ctx_t *req_ctx_p = (expgw_ctx_t*) this;
   int local;
   expgw_attr_cache_t *cache_attr_entry_p ;
    
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
   ** check if the attributes of the parent are still associated with the export gateway
   ** it might be possible that it does not belong anymore to the export gateway if there was
   ** a change in the configuration of the export gateway
   */
   local =  expgw_check_local(ret->hdr.eid,(unsigned char *)ret->parent_attr.ep_mattr_ret_t_u.attrs.fid);
   while (local==0)
   {
      cache_attr_entry_p = NULL;
      com_cache_entry_t  *cache_entry;
      /*
      ** do the lookup on the attributes cache
      */
      cache_attr_entry_p = com_cache_bucket_search_entry(expgw_attr_cache_p,ret->parent_attr.ep_mattr_ret_t_u.attrs.fid);
      if (cache_attr_entry_p != NULL)
      {
        /*
        ** entry has been found, copy the attributes in the response buffer
        */
        memcpy(&cache_attr_entry_p->attr,&ret->parent_attr.ep_mattr_ret_t_u.attrs,  sizeof (mattr_t));
        break;
      }
      cache_entry = expgw_attr_alloc_entry((mattr_t*)&ret->parent_attr.ep_mattr_ret_t_u.attrs);  
      if (cache_entry == NULL)
      {        
        break;
      }
      if (com_cache_bucket_insert_entry(expgw_attr_cache_p, cache_entry) < 0)
      {
        severe("error on fid insertion"); 
        expgw_attr_release_entry(cache_entry->usr_entry_p);
      }
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
      expgw_reply_error(req_ctx_p,ret->status_gw.ep_fid_ret_t_u.error);    
    }
    
out:
    /*
    ** release the gateway context
    */   
    expgw_release_context(req_ctx_p);
}

