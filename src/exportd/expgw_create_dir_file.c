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
#include <rozofs/rpc/export_profiler.h>
#include <rozofs/rpc/epproto.h>
#include <rozofs/rpc/eproto.h>

#include "expgw_export.h"
#include "expgw_fid_cache.h"
#include "expgw_attr_cache.h"
#include "export.h"
#include "volume.h"
#include "exportd.h"
#include <rozofs/core/expgw_common.h>


void expgw_mkdir_mknod_cbk(void *this,void *buffer) ;
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
*   exportd mknod (create a new regular file)

   note: retval is associated to the operation performed by the callback 
    associated with the service (see expgw_mkdir_mknod_cbk()
    
    @param args : fid of the parent and object name 
    
    @retval: EP_SUCCESS :attributes of the parent and of the object
    @retval: EP_FAILURE :error code associated with the operation (errno)
*/
void expgw_mknod_1_svc(epgw_mknod_arg_t * arg, expgw_ctx_t *req_ctx_p) 
{
    int   export_lbg_id;
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
    ** if the fid is not handled by it must operates in passthrough mode
    ** and must not update the cache
    */
    local = expgw_check_local(arg->arg_gw.eid,(unsigned char *)arg->arg_gw.parent);
    if (local != 0)
    {
       /*
       ** the export gateway must operate in passthrough mode
       */
       status = expgw_routing_rq_common(req_ctx_p,arg->arg_gw.eid,(unsigned char *)arg->arg_gw.parent,0,0,expgw_mkdir_mknod_cbk,req_ctx_p);
       if (status < 0)
       {
         goto error;
       }
       goto out;
    }
    /*
    ** the fid parent is local to the export gateway
    ** pre-allocate a cache fid entry: this is needed to update the number of children in the attributes
    ** of the parent: the system will find out the parent fid since it belongs to the key of the fid cache (for the child)
    */
    req_ctx_p->fid_cache_entry = expgw_fid_alloc_entry((unsigned char*)arg->arg_gw.parent,arg->arg_gw.name,NULL);
    status = expgw_forward_rq_common(req_ctx_p,export_lbg_id,0,0,expgw_mkdir_mknod_cbk,req_ctx_p);
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
*   exportd gateway symlink: create a symbolic link

    note: retval is associated to the operation performed by the callback 
    associated with the service (see expgw_mkdir_mknod_cbk()

    @param args : fid of the parent and symlink name  and target link (name)
    
    @retval: EP_SUCCESS :attributes of the parent and  of the created symlink
    @retval: EP_FAILURE :error code associated with the operation (errno)
*/
void expgw_symlink_1_svc(epgw_symlink_arg_t * arg, expgw_ctx_t *req_ctx_p) 
{
    int   export_lbg_id;
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
    ** if the fid is not handled by it must operates in passthrough mode
    ** and must not update the cache
    */
    local = expgw_check_local(arg->arg_gw.eid,(unsigned char *)arg->arg_gw.parent);
    if (local != 0)
    {
       /*
       ** the export gateway must operate in passthrough mode
       */
       status = expgw_routing_rq_common(req_ctx_p,arg->arg_gw.eid,(unsigned char *)arg->arg_gw.parent,0,0,expgw_mkdir_mknod_cbk,req_ctx_p);
       if (status < 0)
       {
         goto error;
       }
       goto out;
    }
    /*
    ** the fid parent is local to the export gateway
    ** pre-allocate a cache fid entry: this is needed to update the number of children in the attributes
    ** of the parent: the system will find out the parent fid since it belongs to the key of the fid cache (for the child)
    */
    req_ctx_p->fid_cache_entry = expgw_fid_alloc_entry((unsigned char*)arg->arg_gw.parent,arg->arg_gw.name,NULL);
    status = expgw_forward_rq_common(req_ctx_p,export_lbg_id,0,0,expgw_mkdir_mknod_cbk,req_ctx_p);
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
*   exportd link

    note: retval is associated to the operation performed by the callback 
    associated with the service (see expgw_mkdir_mknod_cbk()
    
    @param args : fid of the parent and object name and the target inode (fid) 
    
    @retval: EP_SUCCESS :attributes of the parent and of the object
    @retval: EP_FAILURE :error code associated with the operation (errno)
*/
void expgw_link_1_svc(epgw_link_arg_t * arg, expgw_ctx_t *req_ctx_p) 
{
    int   export_lbg_id;
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
    ** if the fid is not handled by it must operates in passthrough mode
    ** and must not update the cache
    */
    local = expgw_check_local(arg->arg_gw.eid,(unsigned char *)arg->arg_gw.newparent);
    if (local != 0)
    {
       /*
       ** the export gateway must operate in passthrough mode
       */
       status = expgw_routing_rq_common(req_ctx_p,arg->arg_gw.eid,(unsigned char *)arg->arg_gw.newparent,0,0,expgw_mkdir_mknod_cbk,req_ctx_p);
       if (status < 0)
       {
         goto error;
       }
       goto out;
    }
    /*
    ** the fid parent is local to the export gateway
    ** pre-allocate a cache fid entry: this is needed to update the number of children in the attributes
    ** of the parent: the system will find out the parent fid since it belongs to the key of the fid cache (for the child)
    */
    req_ctx_p->fid_cache_entry = expgw_fid_alloc_entry((unsigned char*)arg->arg_gw.newparent,arg->arg_gw.newname,NULL);
    status = expgw_forward_rq_common(req_ctx_p,export_lbg_id,0,0,expgw_mkdir_mknod_cbk,req_ctx_p);
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
*   exportd mkdir (create a new directory)

    note: retval is associated to the operation performed by the callback 
    associated with the service (see expgw_mkdir_mknod_cbk()
    
    @param args : fid of the parent and object name 
    
    @retval: EP_SUCCESS :attributes of the parent and of the object
    @retval: EP_FAILURE :error code associated with the operation (errno)
*/
void expgw_mkdir_1_svc(epgw_mkdir_arg_t * arg, expgw_ctx_t *req_ctx_p) 
{
    int   export_lbg_id;
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
    ** if the fid is not handled by it must operates in passthrough mode
    ** and must not update the cache
    */
    local = expgw_check_local(arg->arg_gw.eid,(unsigned char *)arg->arg_gw.parent);
    if (local != 0)
    {
       /*
       ** the export gateway must operate in passthrough mode
       */
       status = expgw_routing_rq_common(req_ctx_p,arg->arg_gw.eid,(unsigned char *)arg->arg_gw.parent,0,0,expgw_mkdir_mknod_cbk,req_ctx_p);
       if (status < 0)
       {
         goto error;
       }
       goto out;
    }
    /*
    ** the fid parent is local to the export gateway
    ** pre-allocate a cache fid entry: this is needed to update the number of children in the attributes
    ** of the parent: the system will find out the parent fid since it belongs to the key of the fid cache (for the child)
    */
    req_ctx_p->fid_cache_entry = expgw_fid_alloc_entry((unsigned char*)arg->arg_gw.parent,arg->arg_gw.name,NULL);
    status = expgw_forward_rq_common(req_ctx_p,export_lbg_id,0,0,expgw_mkdir_mknod_cbk,req_ctx_p);
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

    
    @retval: EP_SUCCESS :attributes of the parent and of the object
    @retval: EP_FAILURE :error code associated with the operation (errno)
 
 @return none
 */
void expgw_mkdir_mknod_cbk(void *this,void *buffer) 
{
   epgw_mattr_ret_t *ret ;
   expgw_ctx_t *req_ctx_p = (expgw_ctx_t*) this;
   int local;
   expgw_attr_cache_t *cache_attr_entry_p = NULL;
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
   ** check if there was a miss for the <fid,name>  case. In that case
   ** we must find a pre-allocated context in the gateway export context
   ** However before inserting it in the cache, we must do a lookup to avoid
   ** double insertion.
   ** In case of insertion failure, it is not required to release that pre-allocated cache context
   ** in case of insertion success; the reference MUST BE REMOVED for the gateway context since
   ** in the release if the field for fid cache contetx is not empty, the system attempts to release
   ** that pre-allocated context
   */ 
   while (req_ctx_p->fid_cache_entry != NULL)
   {
     com_cache_entry_t  *cache_entry = req_ctx_p->fid_cache_entry;
     expgw_fid_cache_t  *cache_fid_entry = cache_entry->usr_entry_p;
       
     local =  expgw_check_local(ret->hdr.eid,cache_fid_entry->key.pfid);
     if (local != 0) break; 
     /*
     ** update the attributes of the parent
     ** if the entry is found, just update the content otherwise allocate an entry from the attribute cache
     */
     while (1)
     {
       com_cache_entry_t  *cache_entry;
       cache_attr_entry_p = NULL;
       cache_attr_entry_p = com_cache_bucket_search_entry(expgw_attr_cache_p,(unsigned char *)ret->parent_attr.ep_mattr_ret_t_u.attrs.fid);
       if (cache_attr_entry_p != NULL)
       {
         /*
         ** entry has been found, update the attributes
         */
         memcpy(&cache_attr_entry_p->attr,&ret->parent_attr.ep_mattr_ret_t_u.attrs, sizeof (mattr_t));
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
      ** update the FID cache
      */
      expgw_fid_cache_t *curr_cache_fid_entry_p = NULL;
      /*
      ** do the lookup to avoid double insertion
      */
      curr_cache_fid_entry_p = com_cache_bucket_search_entry(expgw_fid_cache_p,cache_entry->usr_key_p);
      if (curr_cache_fid_entry_p == NULL)
      {
        /*
        ** nothing found, so insert it
        */ 
        if (com_cache_bucket_insert_entry(expgw_fid_cache_p, cache_entry) < 0)
        {
           severe("error on fid insertion"); 
           break;          
        }
        curr_cache_fid_entry_p = req_ctx_p->fid_cache_entry ;
        req_ctx_p->fid_cache_entry = NULL;
      }
      if (curr_cache_fid_entry_p == NULL) break;
      /*
      ** update the fid entry (copy the child fid)
      */
      memcpy(curr_cache_fid_entry_p->fid,ret->status_gw.ep_mattr_ret_t_u.attrs.fid,sizeof(fid_t));   
      break;          
   }
   /*
   ** check the case of the cache attributes of the child
   */
   local =  expgw_check_local(ret->hdr.eid,(unsigned char *)ret->status_gw.ep_mattr_ret_t_u.attrs.fid);
   while (local==0)
   {
      cache_attr_entry_p = NULL;
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
        memcpy(&cache_attr_entry_p->attr,&ret->status_gw.ep_mattr_ret_t_u.attrs, sizeof (mattr_t));
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
      expgw_reply_error(req_ctx_p,ret->status_gw.ep_mattr_ret_t_u.error);    
    }
    
out:
    /*
    ** release the gateway context
    */   
    expgw_release_context(req_ctx_p);
}

