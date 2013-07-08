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

#ifndef _EXPORT_EXPGW_CONF_H
#define _EXPORT_EXPGW_CONF_H

#include <stdint.h>
#include <limits.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/list.h>
 #include <pthread.h>
#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>

#include "config.h"
#include "export.h"
#include "econfig.h"


extern list_t expgws;
extern pthread_rwlock_t expgws_lock;

/** a managed expgw storage */
typedef struct expgw_storage {
    int  gwid; ///< expgw storage identifier
    char host[ROZOFS_HOSTNAME_MAX]; ///< expgw storage host name
    uint8_t status; ///< status (0 = off line)
    list_t list; ///< used to chain storages
} expgw_storage_t;


int expgw_root_initialize();

/** initialize a volume storage
 *
 * @param vs: the volume storage to initialize
 * @param gwid: identifier to set
 * @param hostname : hostname to set (memory is copied)
 */
void expgw_storage_initialize(expgw_storage_t * vs, int  gwid,
        const char *hostname);

/** release a volume storage
 *
 * has no effect for now
 *
 * @param vs: the volume storage to release
 */
void expgw_storage_release(expgw_storage_t *vs);

/** a expgw of volume storages
 *
 * volume storages are gather in expgw of volume storage
 * each volume storage should be of the same capacity
 */
typedef struct expgw {
    int daemon_id; ///< expgw identifier
    list_t expgw_storages; ///< list of expgw storages managed in the meta expgw
    pthread_rwlock_t lock; ///< lock to be used by export
} expgw_t;

/** initialize a expgw
 *
 * @param expgw: the expgw to initialize
 * @param egwid: the id to set
 */
int expgw_initialize(expgw_t *expgw, int egwid);



/** release a expgw
 *
 * empty the list of volume storages and free each entry
 * (assuming entry were well allocated)
 *
 * @param expgw: the expgw to release
 */
void expgw_release();


/** release all the export expgws
 *
 * empty the list of volume storages and free each entry
 * (assuming entry were well allocated)
 *
 * @param expgw: the expgw to release
 */
void export_expgws_release();

/*
**_________________________________________________________
*/
int load_export_expgws_conf();

/*
**_________________________________________________________
*/
/**
*  that procedure is intended to return the pointer to  the
   expgw context whose identifier is egwid
   
   @param egwid : expgw identifier
   
   @retval <> NULL: expgw context
   @retval NULL not found 
*/
expgw_t *expgws_lookup_expgw(int egwid);
#endif
