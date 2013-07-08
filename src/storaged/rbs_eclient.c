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
 * @return: 0 on success -1 otherwise (errno is set)
 */
int rbs_get_cluster_list(rpcclt_t * clt, const char *export_host, cid_t cid,
        list_t * cluster_entries) {
    int status = -1;
    epgw_cluster_ret_t *ret = 0;
    int i = 0;

    DEBUG_FUNCTION;

    struct timeval timeo;
    timeo.tv_sec = RBS_TIMEOUT_EPROTO_REQUESTS;
    timeo.tv_usec = 0;

    // Initialize connection with exportd server
    if (rpcclt_initialize
            (clt, export_host, EXPORT_PROGRAM, EXPORT_VERSION,
            ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE, 0, timeo) != 0)
        goto out;

    // Send request
    ret = ep_list_cluster_1(&cid, clt->client);
    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }

    if (ret->status_gw.status == EP_FAILURE) {
        errno = ret->status_gw.ep_cluster_ret_t_u.error;
        // Release connection
        rpcclt_release(clt);
        goto out;
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

        // Add this storage to the list of storages for this cluster
        list_push_back(&cluster->storages, &stor->list);
    }

    // Add this cluster to the list of clusters
    list_push_back(cluster_entries, &cluster->list);

    // Release connection
    rpcclt_release(clt);

    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_cluster_ret_t, (char *) ret);
    return status;
}
