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


#ifndef _MCLIENT_H
#define _MCLIENT_H

#include <uuid/uuid.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/xmalloc.h>
#include "rpcclt.h"
#include "mproto.h"

#define MAX_MCLIENT_NAME 8

typedef struct mclient {
    /*
    ** Host may contain several addresses initialy separated by '/'
    ** After parsing, '/' are replaced by 0 and name_idx contains 
    ** the list of indexes in host where the differents other names 
    ** start. Example:
    ** host is initialy "192.168.10.1/192.168.11.1/192.168.12.1\0"
    ** after parsing 
    ** host contains "192.168.10.1\0192.168.11.1\0192.168.12.1\0"
    ** nb_names is 3
    ** and name_idx is [0,13,26,...]
    */
    char    host[ROZOFS_HOSTNAME_MAX];
    uint8_t nb_names;
    uint8_t name_idx[MAX_MCLIENT_NAME]; 
    cid_t cid;
    sid_t sid;
    int status;
    rpcclt_t rpcclt;
} mclient_t;

/*_________________________________________________________________
** Initialize a mclient context. 
** The mclient context has already been allocated
** 
** @param  clt    The allocated mclient context
** @param  host   The host names ('/' separated list of addresses)
** @param  cid    The cluster identifier
** @param  sid    The storage identifier within the cluster
**
** @retval        The total number of names found in host (at least 1)
*/
static inline int mclient_new(mclient_t *clt, char * host, cid_t cid, sid_t sid) {
  char * pChar;

  // Reset RPC context
  init_rpcctl_ctx(&clt->rpcclt);

  strcpy(clt->host,host);
  clt->nb_names = 0;
  pChar         = clt->host;
  
  // Skip eventual '/' at the beginning
  while (*pChar == '/') pChar++;
  if (*pChar == 0) {
    severe("Bad mclient name %s",host);
    return 0;
  }
  
  // Register 1rst name
  clt->name_idx[clt->nb_names++] = (pChar - clt->host);
  
  // Loop on searching for extra names
  while (clt->nb_names < MAX_MCLIENT_NAME) {
  
    // end of string parsing
    if (*pChar == 0)   break;
    
    // name separator
    if (*pChar == '/') {
      *pChar = 0;             // Replace separator 
      pChar++;                // Next character should be next name starting
      if (*pChar == 0) break; // This was actually the end of the string !!!
      clt->name_idx[clt->nb_names++] = (pChar - clt->host); // Save starting index of name
    } 
    
    pChar++;
  } 
  
  clt->cid = cid;
  clt->sid = sid;
  return (clt->nb_names);
}
/*_________________________________________________________________
** Allocate and initialize a mclient context.
** 
** @param  host   The host names ('/' separated list of addresses)
** @param  cid    The cluster identifier
** @param  sid    The storage identifier within the cluster
**
** @retval        The mclient context or NULL
*/
static inline mclient_t * mclient_allocate(char * host, cid_t cid, sid_t sid) {

  mclient_t * mclient = xmalloc(sizeof (mclient_t));
  if (mclient == NULL) return NULL;
  
  mclient_new(mclient, host, cid, sid);
  return mclient;
}

/*_________________________________________________________________
** try to connect a mclient
** 
** @param  clt      The mclient to connect
** @param  timeout  The connection timeout
**
** @retval          0 when connected / -1 when failed
*/
int mclient_connect(mclient_t *clt, struct timeval timeout);

void mclient_release(mclient_t *clt);

int mclient_stat(mclient_t *clt, sstat_t *st);

int mclient_remove(mclient_t * clt, fid_t fid);

int mclient_ports(mclient_t * mclt, int * single, mp_io_address_t * io_address_p);

#endif
