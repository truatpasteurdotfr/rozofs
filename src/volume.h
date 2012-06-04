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

#ifndef _VOLUME_H
#define _VOLUME_H

#include <stdint.h>
#include <limits.h>
#include "rozofs.h"
#include "list.h"

typedef struct volume_stat {
    uint16_t bsize;
    uint64_t bfree;
} volume_stat_t;

typedef struct volume_storage {
    sid_t sid; // Storage identifier
    char host[ROZOFS_HOSTNAME_MAX];
    uint8_t status;
    sstat_t stat;
    list_t list;
} volume_storage_t;

void volume_storage_initialize(volume_storage_t * vs, uint16_t sid,
        const char *hostname);

typedef struct cluster {
    cid_t cid; // Cluster identifier
    uint64_t size;
    uint64_t free;
    list_t storages;
    list_t list;
} cluster_t;

void cluster_initialize(cluster_t *cluster, cid_t cid, uint64_t size,
        uint64_t free);

typedef struct volume {
    vid_t vid; // Volume identifier
    list_t clusters; // Cluster(s) list
    pthread_rwlock_t lock;
} volume_t;

int volume_initialize(volume_t *volume, vid_t vid);

void volume_release(volume_t *volume);

void volume_balance(volume_t *volume);

char *lookup_volume_storage(volume_t *volume, sid_t sid, char *host);

int volume_distribute(volume_t *volume, uint16_t * cid, uint16_t * sids);

void volume_stat(volume_t *volume, volume_stat_t * volume_stat);

#endif
