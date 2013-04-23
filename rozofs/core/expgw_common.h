/*
  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
  This file is part of Rozofs.

  Rozofs is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  Rozofs is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
 */
 
 #ifndef EXPGW_COMMON_H
#define EXPGW_COMMON_H


#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/


#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <uuid/uuid.h>
#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/core/ruc_sockCtl_api.h>
#include <rozofs/core/rozofs_tx_api.h>
#include <rozofs/core/north_lbg_api.h>
#include <rpc/pmap_clnt.h>


#define EXPGW_EID_MAX_IDX 1024

typedef struct _expgw_eid_ctx_t
{
   uint16_t  eid;      /**< eid value  */ 
   uint16_t  exportd_id ; /**< index of the parent exportd  */   
}  expgw_eid_ctx_t;
/**
*  lbg connection towards the EXPORT gateways associated with and exportd_id
*/
#define EXPGW_EXPGW_MAX_IDX 32

typedef struct _expgw_expgw_ctx_t
{
   uint16_t  port;     /**< tcp port in host format   */
   uint32_t  ipaddr;   /**< IP address in host format */
   char      hostname[ROZOFS_HOSTNAME_MAX];
   int       gateway_lbg_id;        /**< reference of the load balancing group for reach the master exportd (default route) */
} expgw_expgw_ctx_t;

#define EXPGW_EXPORTD_MAX_IDX 64

typedef struct _expgw_exportd_ctx_t
{
   uint16_t  exportd_id ; /**< index of the parent exportd  */   
   uint16_t  port;     /**< tcp port in host format   */
   uint32_t  ipaddr;   /**< IP address in host format */
   char      hostname[ROZOFS_HOSTNAME_MAX];
   uint16_t  nb_gateways;  /**< number of gateay for export caching */
   uint16_t  gateway_rank;  /**< rank of the gateway  */
   int       export_lbg_id;        /**< reference of the load balancing group for reach the master exportd (default route) */
   expgw_expgw_ctx_t expgw_list[EXPGW_EXPGW_MAX_IDX];
} expgw_exportd_ctx_t;


extern expgw_eid_ctx_t      expgw_eid_table[];
extern expgw_exportd_ctx_t  expgw_exportd_table[];

/*
**______________________________________________________________________________
*/
/**
* That API must be called before opening the export gateway service

*/
void expgw_export_tableInit();

/*
**______________________________________________________________________________
*/
/**
*  Convert a hostname into an IP v4 address in host format 

@param host : hostname
@param ipaddr_p : return IP V4 address arreay

@retval 0 on success
@retval -1 on error (see errno faor details
*/
int expgw_host2ip(char *host,uint32_t *ipaddr_p);
/*
**______________________________________________________________________________
*/
/**
*  Add an eid entry 

  @param exportd_id : reference of the exportd that mangament the eid
  @param eid: eid within the exportd
  @param hostname: hostname of the exportd
  @param port:   port of the exportd
  @param nb_gateways : number of gateways
  @param gateway_rank : rank of the current export gateway
  
  @retval 0 on success
  @retval -1 on error (see errno for details)
  
*/

int expgw_export_add_eid(uint16_t exportd_id, uint16_t eid, char *hostname, 
                      uint16_t port,uint16_t nb_gateways,uint16_t gateway_rank);
                      
/*
**______________________________________________________________________________
*/
/**
*  update an ienetry with a new gateway count and gateway rank 

  @param eid: eid within the exportd
  @param nb_gateways : number of gateways
  @param gateway_rank : rank of the current export gateway
  
  @retval 0 on success
  @retval -1 on error (see errno for details)
  
*/
int expgw_export_update_eid_gw_info(uint16_t eid,uint16_t nb_gateways,uint16_t gateway_rank);                      

/*
**______________________________________________________________________________
*/
/**
*  check if the fid is to be cached locally 

  @param eid: eid within the exportd
  @param fid : fid of the incoming response or request
  
  @retval 0 local
  @retval ==  1 not local
  @retval <  0 error (see errno for details)
  
*/
int expgw_check_local(uint16_t eid,fid_t fid);


/*
**______________________________________________________________________________
*/
/**
* Get the reference of the LBG for the eid

  @param eid: eid within the exportd
  
  @retval < 0 not found (see errno)
  @retval >= 0 : reference of the load balaning group
  
*/

int expgw_get_exportd_lbg(uint16_t eid);

/*
**______________________________________________________________________________
*/
/**
  That function is inteneded to be called to get the reference of an egress
  load balancing group:
   The system attemps to select the exportd gateway load balancing group.
   If the exportd gateway load balancing group is down, it returns the
   default one which is the load balancinbg group towards
   the master EXPORTD associated with the eid
   
   
  @param eid: eid within the exportd
  @param fid : fid of the incoming response or request
  
  @retval >= 0 : reference of the lood balancing group 
  @retval <  0 no load balancing group 
  
*/
int expgw_get_export_gateway_lbg(uint16_t eid,fid_t fid);

/*
**____________________________________________________
*/
/**
*   API to create a load balancing group towards an exportd

  @param exportclt : context of the exportd (hostname, lbg reference , etc...
  @param prog,vers : rpc program and version needed by port map
  @param port_num: if portnum is not null, port map is not used
  
  @retval  0 on success

*/
int expgw_export_lbg_initialize(expgw_exportd_ctx_t *exportclt ,unsigned long prog,
        unsigned long vers,uint32_t port_num);


/*
**______________________________________________________________________________
*/
/**
*  Add an export gateway entry 

  @param exportd_id : reference of the exportd that mangament the eid
  @param hostname: hostname of the exportd
  @param port:   port of the exportd
  @param gateway_rank : rank of the current export gateway
  
  @retval 0 on success
  @retval -1 on error (see errno for details)
  
*/

int expgw_add_export_gateway(uint16_t exportd_id, char *hostname, 
                             uint16_t port,uint16_t gateway_rank);






/**
*  API for displaying the routing informations

  @param buffer: buffer where to format the output
  @param p : pointer to the cache structure
  @param cache_name: name of the cache
  
  @retval none
*/
static inline char *expgw_display_one_exportd_routing_table(char *buffer,expgw_exportd_ctx_t *p)
{
   int i;

   if ((p->exportd_id  == 0) || (p->nb_gateways == 0)) return buffer;
   
   buffer += sprintf(buffer,"\nexportd %d: nb_gateways/rank %d/%d\n",p->exportd_id,p->nb_gateways,p->gateway_rank);
   buffer += sprintf(buffer,"  default: %s:%d [lbg_id %d]\n",p->hostname,p->port,p->export_lbg_id);
   buffer += sprintf(buffer,"  export gateway routing table: \n");
   for (i = 0; i < p->nb_gateways;i ++)
   {
      if (p->expgw_list[i].hostname[0] == 0) continue;
      buffer += sprintf(buffer,"    [%2.2d]: %s:%d [lbg_id %d]\n",i,p->expgw_list[i].hostname,
                                                                 p->expgw_list[i].port,
                                                                 p->expgw_list[i].gateway_lbg_id);   
   }
   
   return buffer;
}   
    

/**
*  API for displaying the routing informations

  @param buffer: buffer where to format the output
  @param p : pointer to the cache structure
  @param cache_name: name of the cache
  
  @retval none
*/
static inline char *expgw_display_all_exportd_routing_table(char *buffer)
{
   int i;
   expgw_exportd_ctx_t *p = expgw_exportd_table;
   
   for (i = 0; i < EXPGW_EXPORTD_MAX_IDX; i++,p++)
   {
      if (p->exportd_id == 0) continue;
      buffer = expgw_display_one_exportd_routing_table(buffer,p);
   }

   return buffer;
}   
/**
*  API for displaying the association betrween eids and exportd

  @param buffer: buffer where to format the output
  @param p : pointer to the cache structure
  @param cache_name: name of the cache
  
  @retval none
*/

static inline char *expgw_display_all_eid(char *buffer)
{
   int i;
   expgw_eid_ctx_t *p = expgw_eid_table;
   
   for (i = 0; i < EXPGW_EID_MAX_IDX; i++,p++)
   {
      if (p->eid == 0) continue;
      buffer += sprintf(buffer,"eid %4.4d  exportd %d\n",p->eid,p->exportd_id);
   }

   return buffer;
}   


#ifdef __cplusplus
}
#endif /*__cplusplus */




#endif

