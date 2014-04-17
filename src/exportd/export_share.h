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

#ifndef EXPORT_SHARE_H
#define EXPORT_SHARE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <rozofs/common/log.h>
#include <sys/types.h>
#include <unistd.h>


typedef struct export_share_info_t
{
    uint16_t  metadata_port;  /**< listening port for metadata  */
    uint16_t  debug_port;  /**< listening port for metadata  */
    pid_t  pid;            /**< process pid  */
    char      state[128];
    uint32_t  reload_counter;
    uint64_t  uptime;

} export_share_info_t;



/**
* structure for shared memory management
*/
typedef struct _exportd_shared_t
{
   int active;              /**< assert to 1 if the shared memory is in used */
   key_t key;               /**< key of the shared memory pool */
   uint32_t buf_sz;         /**< size of a buffer              */
   uint32_t slave_count;    /**< number of slave exportd              */
   void *data_p;            /**< pointer to the beginning of the shared memory     */
   void *slave_p;            /**<pointer to array corresponding to a slave        */
   int   error;             /**< errno      */
} exportd_shared_t; 


extern exportd_shared_t exportd_shared_mem;

/**
* attach or create the exportd shared memory
  THat shared memory is used to provide the exportd slave statistics
  
  @param p : pointer to the configuration parameters
  
  @retval 0 on success
  @retval -1 on error
*/
int export_sharemem_create_or_attach(exportd_start_conf_param_t *p);

void show_export_slave(char * argv[], uint32_t tcpRef, void *bufRef);

/**
*  increment the reload counter of the exportd slave
  THat shared memory is used to provide the exportd slave statistics
    
  @retval none
*/
void export_sharemem_increment_reload();
/**
*  set the listening port used for metadata for the exportd slave
  THat shared memory is used to provide the exportd slave statistics
  
  @param port: metadata port value
  
  @retval none
*/
void export_sharemem_set_listening_metadata_port(uint16_t port);
/**
*  change the state of the exportd slave
  THat shared memory is used to provide the exportd slave statistics
  
  @param state : new state
  
  @retval none
*/
void export_sharemem_change_state(char *state);
#endif
