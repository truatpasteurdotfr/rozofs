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
#include <stdio.h>
#include <errno.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/list.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/rpc/rpcclt.h>
#include "rozofs/rpc/eproto.h"
#include <rozofs/core/rozofs_host_list.h>

#include "rbs.h"
#include "rbs_eclient.h"

/** Send a request to export server for get the list of member storages
 *  of cluster with a given cid and add this storage list to the list
 *  of clusters
 *
 * @param clt: RPC connection to export server
 * @param export_host: IP or hostname of export server
 * @param cid: the unique ID of cluster
 * @param cluster_entries: list of cluster(s)
 *
 * @return: NULL on error, valid export host name on success
 */
char * rbs_get_cluster_list(rpcclt_t * clt, const char *export_host_list, cid_t cid,
        list_t * cluster_entries) {
    epgw_cluster_ret_t *ret = 0;
    int i = 0;
    int export_idx;
    char * pHost = NULL;
    int retry;

    DEBUG_FUNCTION;

    struct timeval timeo;
    timeo.tv_sec  = 0; 
    timeo.tv_usec = 100000;

    clt->sock = -1;

    /*
    ** Parse host list
    */
    if (rozofs_host_list_parse(export_host_list,'/') == 0) {
        severe("rozofs_host_list_parse(%s)",export_host_list);
    }   
     
    
    for (retry=10; retry > 0; retry--) {
    
        for (export_idx=0; export_idx<ROZOFS_HOST_LIST_MAX_HOST; export_idx++) {

            // Free resources from previous loop
            if (ret) xdr_free((xdrproc_t) xdr_ep_cluster_ret_t, (char *) ret);
	    rpcclt_release(clt);

	    pHost = rozofs_host_list_get_host(export_idx);
	    if (pHost == NULL) break;

        // Initialize connection with exportd server
        if (rpcclt_initialize
            (clt, pHost, EXPORT_PROGRAM, EXPORT_VERSION,
            ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE, 0, timeo) != 0)
        	continue;

        // Send request
        ret = ep_list_cluster_1(&cid, clt->client);
	    if (ret == 0) {
        	errno = EPROTO;
        	continue;
	    }

	    if (ret->status_gw.status == EP_FAILURE) {
        	errno = ret->status_gw.ep_cluster_ret_t_u.error;
        	continue;
	    }

	    // Allocation for the new cluster entry
	    rb_cluster_t *cluster = (rb_cluster_t *) xmalloc(sizeof (rb_cluster_t));
	    cluster->cid = ret->status_gw.ep_cluster_ret_t_u.cluster.cid;

	    // Init the list of storages for this cluster
	    list_init(&cluster->storages);
	    // For each storage member
	    for (i = 0; i < ret->status_gw.ep_cluster_ret_t_u.cluster.storages_nb; i++) {

        	// Init storage
        	rb_stor_t *stor = (rb_stor_t *) xmalloc(sizeof (rb_stor_t));
        	memset(stor, 0, sizeof (rb_stor_t));
        	strncpy(stor->host, ret->status_gw.ep_cluster_ret_t_u.cluster.storages[i].host,
                	ROZOFS_HOSTNAME_MAX);
        	stor->sid = ret->status_gw.ep_cluster_ret_t_u.cluster.storages[i].sid;
        	stor->mclient.rpcclt.sock = -1;

        	// Add this storage to the list of storages for this cluster
        	list_push_back(&cluster->storages, &stor->list);
	    }
	    // Add this cluster to the list of clusters
	    list_push_back(cluster_entries, &cluster->list);

            // Free resources from current loop
            if (ret) xdr_free((xdrproc_t) xdr_ep_cluster_ret_t, (char *) ret);
	    rpcclt_release(clt);
            return pHost;
	}
	
	if (timeo.tv_usec == 100000) {
	  timeo.tv_usec = 500000; 
	}
	else {
	  timeo.tv_usec = 0;
	  timeo.tv_sec++;	
	}  
    }		
    return NULL;
}
