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

/**
* relative index of the export gateway listening ports
*/
typedef enum
{
   EXPGW_PORT_ROZOFSMOUNT_IDX= 0,
   EXPGW_PORT_EXPORTD_IDX,
   EXPGW_PORT_DEBUG_IDX,
   EXPGW_PORT_MAX_IDX,  
} expgw_listening_ports_e;

/**
* state of an entry of the exportd context
*/
typedef enum
{
   EXPGW_STATE_EMPTY= 0,
   EXPGW_STATE_SYNCED,
   EXPGW_STATE_DIRTY,
} expgw_exportd_entry_state_e;

/**
*   The reference of the load balancing group are set to -1 if the destination is not reachable
*   For the case of the master exportd it corresponds to the case where the lbg is down 
*   for the case of the export gateway it correspond to the case of lbg down and also to the case where is eid is not reachable
*/
#define EXPGW_MAX_ROUTING_LBG 2
typedef struct _expgw_tx_routing_ctx_t
{
   int gw_rank;             /**<index of the gateway in the eid/fid routing table                           */
   int eid;                 /**<index of the gateway in the eid/fid routing table                           */
   int cur_lbg;             /**< current lbg idx in the table                                               */
   int nb_lbg;              /**< number of lbg in the table                                                 */
   int lbg_id[EXPGW_MAX_ROUTING_LBG];           /**< reference of the load balancing group of the master Exportd : default route */
   int lbg_type[EXPGW_MAX_ROUTING_LBG];           /**< reference of the load balancing group of the master Exportd : default route */
   int keep_xmit_buf_flag;  /**< assert to one if the requester wants to keep the rpc xmit buffer            */
   void *xmit_buf;          /**< pointer to xmit_buffer when keep_xmit_buf_flag is asserted                  */
} expgw_tx_routing_ctx_t;

//#define EXPGW_EID_MAX_IDX 1024
/**
* note about exp_gateway_status field usage:
* The corresponding bit of an export gateways is cleared for the following cases:
   - the associated load balancing group is down (??)
   - the eid is not handled by the gateway (EP_FAILURE_EID_NOT_SUPPORTED)
   by default all the bits are asserted indicating that the gateway supports the eid. For the
   status of the lbg, it uses the corresponding lbg API 
*/
typedef struct _expgw_eid_ctx_t
{
   uint16_t  eid;      /**< eid value  */ 
   uint16_t  exportd_id ; /**< index of the parent exportd  */   
   uint32_t  exp_gateway_bitmap_status; /**< a 1 indicates that the gateway cannot provide the service this eid  */
   uint64_t  gateway_send_counters[EXPGW_EXPGW_MAX_IDX+1];

}  expgw_eid_ctx_t;
/**
*  lbg connection towards the EXPORT gateways associated with and exportd_id
*/
//#define EXPGW_EXPGW_MAX_IDX 32

typedef struct _expgw_expgw_ctx_t
{
   uint16_t  entry_state;  /**< config state see expgw_exportd_entry_state_e */
   uint16_t  port;     /**< tcp port in host format   */
   uint32_t  ipaddr;   /**< IP address in host format */
   char      hostname[ROZOFS_HOSTNAME_MAX];
   int       gateway_lbg_id;        /**< reference of the load balancing group for reach the master exportd (default route) */
} expgw_expgw_ctx_t;

//#define EXPGW_EXPORTD_MAX_IDX 64

typedef struct _expgw_exportd_ctx_t
{
   uint32_t  hash_config;   /**< hash configuretion of the export file    */
   uint16_t  exportd_id ;   /**< index of the parent exportd              */   
   uint16_t  port;          /**< tcp port in host format                  */
   uint32_t  ipaddr;        /**< IP address in host format                */
   char      hostname[ROZOFS_HOSTNAME_MAX];
   uint16_t  nb_gateways;  /**< number of gateay for export caching        */
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
* init of the routing context for export gateway and rozofsmount

  @param p: pointer to the routing context
  
  @retval none
*/
static inline void expgw_routing_ctx_init(expgw_tx_routing_ctx_t *p)
{
   int i;
   for (i = 0; i < EXPGW_MAX_ROUTING_LBG; i++) 
   {
     p->lbg_id[i] = -1;  
     p->lbg_type[i] = -1;  
   }
   p->eid         = 0;
   p->nb_lbg      = 0;       
   p->cur_lbg     = 0;       
   p->keep_xmit_buf_flag = 0; 
   p->xmit_buf = NULL; 
   p->gw_rank = -1;
}

/*
**______________________________________________________________________________
*/
/**
* API to get the current hash config associated with an exportd_id

  @param exportd_id: index of the master exportd
  
  @retval hash value of the configuration (0 if the exportd id is out of range
  
*/
static inline uint32_t expgw_get_exportd_hash_config(uint32_t exportd_id)
{
   if (exportd_id >= EXPGW_EXPORTD_MAX_IDX)
   {
     /*
     ** out of range
     */
     severe("expgw_get_exportd_hash_config: exportd_id is out of range :%d max: %d",exportd_id,EXPGW_EXPORTD_MAX_IDX);
     return 0;   
   }
   return expgw_exportd_table[exportd_id].hash_config;
}


/*
**______________________________________________________________________________
*/
/**
* API to get the current hash config associated with an exportd_id

  @param exportd_id: index of the master exportd
  @param hash_config: hash value of the exportd configuration file
  
  @retval 0 on success
  @retval -1 on error (exportd id out of range)
  
*/
static inline int expgw_set_exportd_hash_config(uint32_t exportd_id,uint32_t hash_config)
{
   if (exportd_id >= EXPGW_EXPORTD_MAX_IDX)
   {
     /*
     ** out of range
     */
     severe("expgw_set_exportd_hash_config: exportd_id is out of range :%d max: %d",exportd_id,EXPGW_EXPORTD_MAX_IDX);
     return -1;   
   }
   expgw_exportd_table[exportd_id].hash_config = hash_config;
   return 0;
}
/*
**______________________________________________________________________________
*/
/**
* Update the gateways statistics


  @param eid : index of the export
  @param lbg_type : 0 for an export gateway and 1 for the deault gateway (master exportd)
  @param gateway_rank: index of the server
  
  @retval none
*/
static inline void expgw_routing_update_stats(int eid,int lbg_type,int gateway_rank)
{
  if (eid >= EXPGW_EID_MAX_IDX) return;
  if ( gateway_rank >= EXPGW_EXPGW_MAX_IDX) return;
  
  if (lbg_type == 0) expgw_eid_table[eid].gateway_send_counters[gateway_rank]++;
  else expgw_eid_table[eid].gateway_send_counters[EXPGW_EXPGW_MAX_IDX]++;

}

/*
**______________________________________________________________________________
*/
/**
*   Get the next lbg_id available

  @param p: exportd lbg routing table
  @param xmit_buffer : buffer used for transmission
  
  @retval >=  0 reference of the load balancing group
  @retval < O no available load balancing group
*/
static inline int expgw_routing_get_next(expgw_tx_routing_ctx_t *p,void *xmit_buf)
{
    int lbg_id;
    if (p->nb_lbg == 0)
    {
      return -1;    
    }
    if (p->nb_lbg == p->cur_lbg)
    {
      return -1;    
    }
    if ((p->cur_lbg == 0) && (p->nb_lbg > 1))
    {
      /*
      ** save the xmit_buffer and assert the inuse flag in the routing context
      */
      p->xmit_buf = xmit_buf;
      ruc_buf_inuse_increment(p->xmit_buf);      
      p->keep_xmit_buf_flag = 1;   
    }
    lbg_id = p->lbg_id[p->cur_lbg];
    /*
    ** update the satistics
    */
    expgw_routing_update_stats(p->eid,p->lbg_type[p->cur_lbg],p->gw_rank);
    p->cur_lbg++;
    /*
    ** return the next available load balancing group reference
    */
    return lbg_id;
}
/*
**______________________________________________________________________________
*/
/**
*   Invalidate the usage of an export gateway for a given eid

  @param p: exportd lbg routing table
  @param eid: eid to validate/invalidate
  @param action : EXPGW_SUPPORTS_EID/EXPGW_DOES_NOT_SUPPORT_EID
    
  @retval >=  0 reference of the load balancing group
  @retval < O no available load balancing group
*/
#define EXPGW_SUPPORTS_EID 1
#define EXPGW_DOES_NOT_SUPPORT_EID 0
static inline int expgw_routing_expgw_for_eid(expgw_tx_routing_ctx_t *p, int eid, int action)
{
    int srv_rank;
    /*
    ** Bad gateway rank is saved in this routing context
    */
    srv_rank = p->gw_rank ;
    if ((srv_rank < -1) || (srv_rank >= EXPGW_EXPGW_MAX_IDX))
    {
      errno = EINVAL;
      return -1;    
    }
    /*
    ** Bad eid value
    */
    if (eid >= EXPGW_EXPORTD_MAX_IDX)
    {
      errno = EINVAL;
      return -1;
    }
    
    /*
    ** Set bit to one for this gateway
    */
    if (action == EXPGW_SUPPORTS_EID) {
      /*
      ** Validate the gateway by clearing bit
      */
      expgw_eid_table[eid].exp_gateway_bitmap_status &= ~(1<<srv_rank);
    }
    else {
      /*
      ** Invalidate the gateway by setting bit to 1
      */
      expgw_eid_table[eid].exp_gateway_bitmap_status |= (1<<srv_rank);
    }  
    return 0;
}
/*
**______________________________________________________________________________
*/
/**
*   Insert the reference of a load balancing group in the routing context

  @param p: exportd lbg routing table
  @param lbg_id : reference of the load balancing group
  @param eid : reference of export
  @param lbg_type : type of the gateway (exportd is 1)
  
  @retval 0 success
  @retval < 0 error
*/
static inline int expgw_routing_insert_lbg(expgw_tx_routing_ctx_t *p,int lbg_id,int eid,int lbg_type)
{

    if (p->nb_lbg >= EXPGW_MAX_ROUTING_LBG)
    {
      return -1;    
    }
    p->eid = eid;
    p->lbg_id[p->nb_lbg] = lbg_id;
    p->lbg_type[p->nb_lbg] = lbg_type;
    p->nb_lbg++;
    return 0;
}
/*
**______________________________________________________________________________
*/
/**
*  API to attempt releasing an xmit buffer that might have been saved
   when there is more that one available load balancing group
   
   @param p : load balancing group routing table
   
   @retval none
*/
static inline void expgw_routing_release_buffer(expgw_tx_routing_ctx_t *p)
{
  /*
  ** attempt to release the saved buffer, there is saved buffer when
  * keep_xmit_buf_flag is asserted
  */
  if (p->keep_xmit_buf_flag == 0) return;
  if (p->xmit_buf == NULL) return;
  /*
  ** decrement the inuse counter
  */
  int inuse = ruc_buf_inuse_decrement(p->xmit_buf);
  if(inuse == 1) 
  {
    ruc_objRemove((ruc_obj_desc_t*)p->xmit_buf);
    ruc_buf_freeBuffer(p->xmit_buf);
  } 
  p->xmit_buf           = NULL; 
  p->keep_xmit_buf_flag = 0; 
 
}
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
*  API to set to the dirty state all the entry associated with an exportd id
   That API is intended to be called upon receiving a new configuration from 
   an exportd
   
   @param exportd_id : index of the exportd
   
   @retval 0 on success
   @retval -1 : exportd id is out of range
*/
int expgw_set_exportd_table_dirty(uint32_t exportd_id);

/*
**______________________________________________________________________________
*/
/**
*  API to clean up the dirty entries of a exportd id after a configuration update

   The purpose of that service is to release any created load balancing group
   
   @param exportd_id : index of the exportd
   
   @retval 0 on success
   @retval -1 : exportd id is out of range
*/
int expgw_clean_up_exportd_table_dirty(uint32_t exportd_id);
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
   int i,j;
   expgw_eid_ctx_t *p = expgw_eid_table;
   expgw_exportd_ctx_t *q = expgw_exportd_table;
   int nb_gateways;
   int lbg_id;
   
   for (i = 0; i < EXPGW_EID_MAX_IDX; i++,p++)
   {
      if (p->eid == 0) continue;
      q = &expgw_exportd_table[p->exportd_id];
      nb_gateways = q->nb_gateways;
      
      buffer += sprintf(buffer,"eid %4.4d  exportd %d\n",p->eid,p->exportd_id);
//      if (nb_gateways == 0) continue;
      for (j= 0; j < nb_gateways; j++)
      {
        buffer += sprintf(buffer," rank %3d : %8llu ",j,(long long unsigned int)p->gateway_send_counters[j]);              
        if (q->expgw_list[j].hostname[0] == 0) 
        {
         buffer += sprintf(buffer,"\n");
         continue;
        }
        if (q->gateway_rank == j)
        {
           buffer += sprintf(buffer," *%15s:%5.5d\n",q->expgw_list[j].hostname,q->expgw_list[j].port);  
           continue;              
        }
        buffer += sprintf(buffer," %16s:%5.5d ",q->expgw_list[j].hostname,q->expgw_list[j].port);
        lbg_id = q->expgw_list[j].gateway_lbg_id;
        if (lbg_id== -1)
        {
           buffer += sprintf(buffer," lbg_id ??? state DOWN ");     
        }
        else
        {
           int lbg_state = north_lbg_get_state(lbg_id);
           switch (lbg_state)
           {
             case NORTH_LBG_UP:
              buffer += sprintf(buffer," lbg_id %3d state UP   ",lbg_id);  
              break; 
              default:
             case NORTH_LBG_DOWN:
              buffer += sprintf(buffer," lbg_id %3d state DOWN ",lbg_id);  
              break;          
           }
        }
        /*
        ** provide the lock state
        */
        if ((p->exp_gateway_bitmap_status & (1<<i)) == 0)
        {
          buffer += sprintf(buffer," unblocked ");        
        }
        else
        {
          buffer += sprintf(buffer," blocked ");        
        }
        buffer += sprintf(buffer,"\n");
      }
      buffer += sprintf(buffer," default  : %8llu ",(long long unsigned int)p->gateway_send_counters[EXPGW_EXPGW_MAX_IDX]);
      buffer += sprintf(buffer," %16s:%5.5d ",q->hostname,q->port);
      lbg_id = q->export_lbg_id;
      if (lbg_id== -1)
      {
         buffer += sprintf(buffer," lbg_id ??? state DOWN ");     
      }
      else
      {
         int lbg_state = north_lbg_get_state(lbg_id);
         switch (lbg_state)
         {
           case NORTH_LBG_UP:
            buffer += sprintf(buffer," lbg_id %3d state UP   ",lbg_id);  
            break; 
            default:
           case NORTH_LBG_DOWN:
            buffer += sprintf(buffer," lbg_id %3d state DOWN ",lbg_id);  
            break;          
         }
         if (north_lbg_get_state(lbg_id) != NORTH_LBG_UP)
         buffer += sprintf(buffer," lbg_id ??? state DOWN ");     
      }
      buffer += sprintf(buffer,"\n");     
      
   }

   return buffer;
}   


/*
**______________________________________________________________________________
*/
/**
  That function is inteneded to be called to get the references of the egress
  load balancing group:

  That API might return up to 2 load balancing group references
  When there are 2 references the first is the one associated with the exportd gateway
  and the second is the one associated with the master exportd (default route).
  
  Note: for the case of the default route the state of the load balancing group
  is not tested. This might avoid a reject of a request while the system attempts
  to reconnect. This will permit the offer a system which is less sensitive to
  the network failures.
   
   
  @param eid: eid within the exportd
  @param fid : fid of the incoming response or request
  @param routing_ctx_p : load balancing routing context result
  
  @retval >= 0 : reference of the lood balancing group 
  @retval <  0 no load balancing group 
  
*/
int expgw_get_export_routing_lbg_info(uint16_t eid,fid_t fid,expgw_tx_routing_ctx_t *routing_ctx_p);


#ifdef __cplusplus
}
#endif /*__cplusplus */




#endif

