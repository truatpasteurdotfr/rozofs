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
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <pthread.h>
#include <rozofs/rpc/export_profiler.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/rpc/geo_replica_proto.h>
#include "geo_replica_proto_nb.h"
#include "geo_replica_srv.h"

#include "export.h"
#include "volume.h"
#include "exportd.h"
#include "geo_profiler.h"


#define EXPORTS_SEND_REPLY(req_ctx_p) \
     req_ctx_p->xmitBuf  = req_ctx_p->recv_buf; \
    req_ctx_p->recv_buf = NULL; \
    rozorpc_srv_forward_reply(req_ctx_p,(char*)&ret); \
    rozorpc_srv_release_context(req_ctx_p);

#define EXPORTS_SEND_REPLY_WITH_RET(req_ctx_p,ret) \
     req_ctx_p->xmitBuf  = req_ctx_p->recv_buf; \
    req_ctx_p->recv_buf = NULL; \
    rozorpc_srv_forward_reply(req_ctx_p,(char*)ret); \
    rozorpc_srv_release_context(req_ctx_p);
    
uint32_t geo_profiler_eid = 0;    
geo_one_profiler_t  * geo_profiler[EXPGW_EID_MAX_IDX+1] = { 0 };
 /*
**______________________________________________________________________________
*/
void geo_null_1_svc_nb(void * pt, rozorpc_srv_ctx_t *req_ctx_p) {
    static void *ret = NULL;
     EXPORTS_SEND_REPLY(req_ctx_p);
    return;
}

/*
**______________________________________________________________________________
*/
/**
*   geo-replication: attempt to get some file to replicate

    @param req_ctx_p.eid : reference of the exportd
    @param req_ctx_p.site_id : source site identifier
    @param req_ctx_p.local_ref :local reference of the caller
    
   @retval 

*/
void geo_sync_req_1_svc_nb(void * pt, rozorpc_srv_ctx_t *req_ctx_p) 
{
    static geo_sync_req_ret_t ret;
    geo_sync_req_arg_t * arg = (geo_sync_req_arg_t *)pt; 
    export_t *exp;
    int error = 0;
    DEBUG_FUNCTION;

    // Set profiler export index
    geo_profiler_eid = arg->eid;

    START_GEO_PROFILING(geo_sync_req);
    /**
    * get the exportd context
    */
    if (!(exp = exports_lookup_export(arg->eid)))
        goto error;

    if (geo_replicat_sync_req
            (exp, arg->site_id, arg->local_ref,
            (geo_sync_data_ret_t *) & ret.geo_sync_req_ret_t_u.data) != 0)
        goto error;
     ret.status = GEO_SUCCESS;
     goto out;
error:
     ret.status = GEO_FAILURE;
     ret.geo_sync_req_ret_t_u.error = errno;
     error = errno;
out:
    EXPORTS_SEND_REPLY(req_ctx_p);
    STOP_GEO_PROFILING(geo_sync_req,error);
    return ;
}


/*
**______________________________________________________________________________
*/
/**
*   geo-replication: get the next records of the file for which the client
    get a positive acknowledgement for file synchronization

    @param req_ctx_p.eid : reference of the exportd
    @param req_ctx_p.site_id : source site identifier
    @param req_ctx_p.local_ref :local reference of the caller
    @param req_ctx_p.remote_ref :reference provided by the serveur
    @param req_ctx_p.next_record : nex record to get (first)
    @param req_ctx_p.filename :filename of the geo-synchro file
    
   @retval 

*/
void geo_sync_get_next_req_1_svc_nb(void * pt, rozorpc_srv_ctx_t *req_ctx_p) 
{
    static geo_sync_req_ret_t ret;
    geo_sync_get_next_req_arg_t * arg = (geo_sync_get_next_req_arg_t *)pt; 
    export_t *exp;
    int error = 0;
    DEBUG_FUNCTION;

    // Set profiler export index
    geo_profiler_eid = arg->eid;

    START_GEO_PROFILING(geo_sync_get_next_req);
    /**
    * get the exportd context
    */
    if (!(exp = exports_lookup_export(arg->eid)))
        goto error;

    if (geo_replicat_get_next
            (exp, arg->site_id, arg->local_ref,arg->remote_ref,
	    arg->next_record,arg->file_idx,
	    arg->status_bitmap,
            (geo_sync_data_ret_t *) & ret.geo_sync_req_ret_t_u.data) != 0)
        goto error;
     ret.status = GEO_SUCCESS;
     errno = 0;
     goto out;
error:
     ret.status = GEO_FAILURE;
     ret.geo_sync_req_ret_t_u.error = errno;
     error = errno;
out:
    EXPORTS_SEND_REPLY(req_ctx_p);
    STOP_GEO_PROFILING(geo_sync_get_next_req,error);
    return ;
}

/*
**______________________________________________________________________________
*/
/**
*   geo-replication: ask for file deletion upon end of synchronization of file

    @param req_ctx_p.eid : reference of the exportd
    @param req_ctx_p.site_id : source site identifier
    @param req_ctx_p.local_ref :local reference of the caller
    @param req_ctx_p.remote_ref :reference provided by the serveur
    @param req_ctx_p.filename :filename of the geo-synchro file
    
   @retval 

*/
void geo_sync_delete_req_1_svc_nb(void * pt, rozorpc_srv_ctx_t *req_ctx_p) 
{
    static geo_status_ret_t ret;
    geo_sync_delete_req_arg_t * arg = (geo_sync_delete_req_arg_t *)pt; 
    export_t *exp;
    int error = 0;
    DEBUG_FUNCTION;

    // Set profiler export index
    geo_profiler_eid = arg->eid;

    START_GEO_PROFILING(geo_sync_delete_req);
    /**
    * get the exportd context
    */
    if (!(exp = exports_lookup_export(arg->eid)))
        goto error;
    if (geo_replicat_delete
            (exp, arg->site_id, arg->local_ref,arg->remote_ref,
	    arg->file_idx) != 0)
        goto error;
     ret.status = GEO_SUCCESS;
     goto out;
error:
     ret.status = GEO_FAILURE;
     ret.geo_status_ret_t_u.error = errno;
     error = errno;
out:
    EXPORTS_SEND_REPLY(req_ctx_p);
    STOP_GEO_PROFILING(geo_sync_delete_req,error);
    return ;
}

/*
**______________________________________________________________________________
*/
/**
*   geo-replication: ask for file closing upon abort

    @param req_ctx_p.eid : reference of the exportd
    @param req_ctx_p.site_id : source site identifier
    @param req_ctx_p.local_ref :local reference of the caller
    @param req_ctx_p.remote_ref :reference provided by the serveur
    @param req_ctx_p.filename :filename of the geo-synchro file
    
   @retval 

*/
void geo_sync_close_req_1_svc_nb(void * pt, rozorpc_srv_ctx_t *req_ctx_p) 
{
    static geo_status_ret_t ret;
    geo_sync_close_req_arg_t * arg = (geo_sync_close_req_arg_t *)pt; 
    export_t *exp;
    int error = 0;
    DEBUG_FUNCTION;

    // Set profiler export index
    geo_profiler_eid = arg->eid;
   

    START_GEO_PROFILING(geo_sync_close_req);
    /**
    * get the exportd context
    */
    if (!(exp = exports_lookup_export(arg->eid)))
        goto error;
    if (geo_replicat_close
            (exp, arg->site_id, arg->local_ref,arg->remote_ref,
	    arg->file_idx,arg->status_bitmap) != 0)
        goto error;
     ret.status = GEO_SUCCESS;
     goto out;
error:
     ret.status = GEO_FAILURE;
     ret.geo_status_ret_t_u.error = errno;
     error = errno;
out:
    EXPORTS_SEND_REPLY(req_ctx_p);
    STOP_GEO_PROFILING(geo_sync_close_req,error);
    return ;
}
