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

/* need for crypt */
#define _XOPEN_SOURCE 500

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>

#include "eproto.h"
#include "eclient.h"


        

int exportclt_reload(exportclt_t * clt, const char *host, char *root,
        const char *passwd, uint32_t bufsize,
        uint32_t retries,
        ep_mount_ret_t **ret_p) 
{
    int status = -1;
    ep_mount_ret_t *ret = 0;
    char *md5pass = 0;
    int i = 0;
    DEBUG_FUNCTION;

    *ret_p = NULL;
    /* Prepare mount request */
    strcpy(clt->host, host);
    clt->root = strdup(root);
    clt->passwd = strdup(passwd);
    clt->retries = retries;
    clt->bufsize = bufsize;

    /* Initialize connection with export server */
    if (rpcclt_initialize
            (&clt->rpcclt, host, EXPORT_PROGRAM, EXPORT_VERSION,
            ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE, 0) != 0)
        goto out;

    /* Send mount request */
    ret = ep_mount_1(&root, clt->rpcclt.client);
    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_mount_ret_t_u.error;
        goto out;
    }

    /* Check password */
    if (memcmp(ret->ep_mount_ret_t_u.export.md5, ROZOFS_MD5_NONE, ROZOFS_MD5_SIZE) != 0) {
        md5pass = crypt(passwd, "$1$rozofs$");
        if (memcmp(md5pass + 10, ret->ep_mount_ret_t_u.export.md5, ROZOFS_MD5_SIZE) != 0) {
            errno = EACCES;
            goto out;
        }
    }
    *ret_p = ret;
    status = 0;
out:
    if (md5pass)
        free(md5pass);
    if ((status != 0) &&(ret))
        xdr_free((xdrproc_t) xdr_ep_mount_ret_t, (char *) ret);
    return status;
}



int exportclt_reload_check_mstorage(ep_mount_ret_t *ret_mount_p)
{
    int node;
    
    for (node = 0; node < ret->ep_mount_ret_t_u.export.storage_nodes_nb; node++) 
    {
      int i = 0;
      int ret;
      list_t *iterator = NULL;
      int found = 0;
      ep_storage_node_t stor_node = ret->ep_mount_ret_t_u.export.storage_nodes[node];   
      /* Search if the node has already been created  */
      list_for_each_forward(iterator, &exportclt.storages) 
      {
        mstorage_t *s = list_entry(iterator, mstorage_t, list);

        if (strcmp(s->host,stor_node->host) != 0) continue;
        /*
        ** entry is found 
        ** update the cid and sid part only. changing the number of
        ** ports of the mstorage is not yet supported.
        */
        s->sids_nb = stor_node->sids_nb;
        memcpy(s->sids, stor_node->sids, sizeof (sid_t) * stor_node->sids_nb);
        memcpy(s->cids, stor_node.cids, sizeof (cid_t) * stor_node.sids_nb);
        /*
        ** update of the cid/sid<-->lbg_id association table
        */
        for (i = 0; i < s->sids_nb;i++)
        {
          rozofs_storcli_cid_table_insert(1,s->sids,s->lbg_id);       
        }
        found = 1;
        break;
      }
      /*
      ** Check if the node has been found in the configuration. If it is the
      ** case, check the next node the the mount response 
      */
      if (found) continue;
      /*
      ** This is a new node, so create a new entry for it
      */
      mstorage_t *mstor = (mstorage_t *) xmalloc(sizeof (mstorage_t));
      memset(mstor, 0, sizeof (mstorage_t));
      mstor->lbg_id = -1;  /**< lbg not yet allocated */
      strcpy(mstor->host, stor_node.host);
      mstor->sids_nb = stor_node.sids_nb;
      memcpy(mstor->sids, stor_node.sids, sizeof (sid_t) * stor_node.sids_nb);
      memcpy(mstor->cids, stor_node.cids, sizeof (cid_t) * stor_node.sids_nb);

      /* Add to the list */
      list_push_back(&clt->storages, &mstor->list);
      /*
      ** Now create the load balancing group associated with that node and
      ** attempt to get its port configuration
      */        
      mclient_t mclt;
      strcpy(mclt.host, mstor->host);
      uint32_t ports[STORAGE_NODE_PORTS_MAX];
      memset(ports, 0, sizeof (uint32_t) * STORAGE_NODE_PORTS_MAX);
      /*
      ** allocate the load balancing group for the mstorage
      */
      mstor->lbg_id = north_lbg_create_no_conf();
      if (mstor->lbg_id < 0)
      {
        severe(" out of lbg contexts");
        goto fatal;        
      }
      /* Initialize connection with storage (by mproto) */
      if (mclient_initialize(&mclt) != 0) 
      {
          fprintf(stderr, "Warning: failed to join storage (host: %s), %s.\n",
                  mstor->host, strerror(errno));
      } 
      else 
      {
        /* Send request to get storage TCP ports */
        if (mclient_ports(&mclt, ports) != 0) 
        {
            fprintf(stderr,
                    "Warning: failed to get ports for storage (host: %s).\n"
                    , mstor->host);
        }
      }
      /* Initialize each TCP ports connection with this storage node
       *  (by sproto) */
      for (i = 0; i < STORAGE_NODE_PORTS_MAX; i++) 
      {
         if (ports[i] != 0) 
         {
             strcpy(s->sclients[i].host, mstor->host);
             mstor->sclients[i].port = ports[i];
             mstor->sclients[i].status = 0;
             mstor->sclients_nb++;
         }
      }
      /*
      ** proceed with storage configuration if the number of port is different from 0
      */
      if (mstor->sclients_nb != 0)
      {
        ret = storaged_lbg_initialize(mstor);
        if (ret < 0)
        {
          goto fatal;                       
        }
      }
      /*
      ** init of the cid/sid<-->lbg_id association table
      */
      for (i = 0; i < mstor->sids_nb;i++)
      {
        rozofs_storcli_cid_table_insert(1,mstor->sids,mstor->lbg_id);       
      }
      /* Release mclient*/
      mclient_release(&mclt);
    }
    return 0;
    

fatal:
    return -1;
}
