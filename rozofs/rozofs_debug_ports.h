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

#ifndef _ROZOFS_DEBUG_PORTS_H
#define _ROZOFS_DEBUG_PORTS_H

#include <stdint.h>
#include <config.h>
#include "rozofs.h"

extern uint16_t rzdbg_default_base_port;

#define RZDBG_DEFAULT_BASE_PORT  50000   /**< default base port for debug service   */
#define RZDBG_ROZOFSMOUNT_MAX_INSTANCES 8

/**
 relative index of rozodebug ports 
 */
#define RZDBG_EXPORTD_PORT 0        /**< Exportd port*/
#define RZDBG_EXPGW_PORT  (RZDBG_EXPORTD_PORT+1)          /**< Export Gateway port*/
#define RZDBG_RBS_PORT    (RZDBG_EXPGW_PORT+1)            /**< Rebuild process port*/
#define RZDBG_ROZOFSMOUNT_PORT (RZDBG_RBS_PORT+1)         /**< index of the first rozofsmount instance port*/
#define RZDBG_STORAGED_PORT    (RZDBG_ROZOFSMOUNT_PORT+ (RZDBG_ROZOFSMOUNT_MAX_INSTANCES*3))  /**< index of the first storaged instance port*/
#define RZDBG_LAST_PORT         RZDBG_STORAGED_PORT+(STORAGE_NODE_PORTS_MAX)

/**
* Get the rozofsmount rozodebug port based on the rozofmount instance 

  @param instance :rozofsmount instance
  
  @retval rozodebug port value
  
*/
static inline uint16_t rzdbg_get_rozofsmount_port(uint8_t instance)
{

  return (rzdbg_default_base_port +RZDBG_ROZOFSMOUNT_PORT +instance*3);

}
/**
* Get the storcli rozodebug port based on the rozofmount instance and storcli instance

  @param rozofs_instance: rozofsmount instance
  @param storcli_instance: storcli instance
  
  @retval rozodebug port value
  
*/
static inline uint16_t rzdbg_get_storcli_port(uint8_t rozofs_instance, uint8_t storcli_instance)
{

  return (rzdbg_default_base_port +RZDBG_ROZOFSMOUNT_PORT +rozofs_instance*3+storcli_instance+1);

}

#endif
