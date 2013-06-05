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

 
#ifndef ROZOFS_STORCLI_RELOAD_STORAGE_CONFIG_H
#define ROZOFS_STORCLI_RELOAD_STORAGE_CONFIG_H


#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/



#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/rpc/eproto.h>
#include <rozofs/rpc/eclient.h>


typedef enum 
{
  STORCLI_CONF_UNKNOWN = 0,
  STORCLI_CONF_NOT_SYNCED ,
  STORCLI_CONF_SYNCED,
  STORCLI_CONF_MAX,
} storcli_conf_state_e;

typedef enum
{
  STORCLI_STATS_ATTEMPT = 0,
  STORCLI_STATS_SUCCESS,
  STORCLI_STATS_FAILURE,
  STORCLI_STATS_MAX
} storcli_conf_stats_e;

typedef struct _storcli_conf_stats_t
{
   uint64_t poll_counter[STORCLI_STATS_MAX];
   uint64_t conf_counter[STORCLI_STATS_MAX];
} storcli_conf_stats_t;

typedef struct storcli_conf_ctx_t
{
   storcli_conf_state_e  conf_state;    /**< configuration  state */  
   storcli_conf_stats_t stats;  /**< statistics */
} storcli_conf_ctx_t;



#define STORCLI_CONF_STATS_INC(p,cpt)  \
{\
   p->stats.cpt[STORCLI_STATS_ATTEMPT]++;\
}

#define STORCLI_CONF_STATS_OK(p,cpt)  \
{\
   p->stats.cpt[STORCLI_STATS_SUCCESS]++;\
}

#define STORCLI_CONF_STATS_NOK(p,cpt)  \
{\
   p->stats.cpt[STORCLI_STATS_FAILURE]++;\
}


extern uint32_t storcli_configuration_file_hash;  /**< hash value of the configuration file */
extern storcli_conf_ctx_t storcli_conf_ctx;      /**< statistics associated with exportd configuration polling and reload */

/*__________________________________________________________________________
*/

/**
 *  API to start the exportd configuration supervision thread
 *
    The goal of that thread is to poll the master exportd for checking
    any change in the configuration and to reload the configuration
    when there is a change
    
 @param clt: pointer to the context that contains the information relation to the exportd local config.
 
 @retval 0 on success
 @retval -1 on error
 */
int rozofs_storcli_start_exportd_config_supervision_thread(exportclt_t * clt);




#ifdef __cplusplus
}
#endif /*__cplusplus */




#endif
