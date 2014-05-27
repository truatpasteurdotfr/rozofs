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

#ifndef GEO_REPLICA_PROTO_NB_H
#define GEO_REPLICA_PROTO_NB_H
#include <rozofs/rozofs.h>
#include <rozofs/core/rozofs_rpc_non_blocking_generic_srv.h>
#include <rozofs/core/ruc_buffer_api.h>
#include <rozofs/rpc/rozofs_rpc_util.h>
#include <rozofs/common/log.h>
#include <rozofs/rpc/geo_replica_proto.h>


 /*
**______________________________________________________________________________
*/
void geo_null_1_svc_nb(void * pt, rozorpc_srv_ctx_t *req_ctx_p);

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
void geo_sync_req_1_svc_nb(void * pt, rozorpc_srv_ctx_t *req_ctx_p);



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
void geo_sync_get_next_req_1_svc_nb(void * pt, rozorpc_srv_ctx_t *req_ctx_p);

/*
**______________________________________________________________________________
*/
/**
*   geo-replication: ask for file deletion upon end of synchronization of file

    @param req_ctx_p.eid : reference of the exportd
    @param req_ctx_p.site_id : source site identifier
    @param req_ctx_p.local_ref :local reference of the caller
    @param req_ctx_p.remote_ref :reference provided by the serveur
    @param req_ctx_p.next_record : nex record to get (first)
    @param req_ctx_p.filename :filename of the geo-synchro file
    
   @retval 

*/
void geo_sync_delete_req_1_svc_nb(void * pt, rozorpc_srv_ctx_t *req_ctx_p);

/*
**______________________________________________________________________________
*/
/**
*   geo-replication: ask for file deletion upon end of synchronization of file

    @param req_ctx_p.eid : reference of the exportd
    @param req_ctx_p.site_id : source site identifier
    @param req_ctx_p.local_ref :local reference of the caller
    @param req_ctx_p.remote_ref :reference provided by the serveur
    @param req_ctx_p.next_record : nex record to get (first)
    @param req_ctx_p.filename :filename of the geo-synchro file
    
   @retval 

*/
void geo_sync_close_req_1_svc_nb(void * pt, rozorpc_srv_ctx_t *req_ctx_p);
#endif
