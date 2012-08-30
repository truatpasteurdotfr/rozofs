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

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <uuid/uuid.h>
#include "rozofs.h"
#include "log.h"
#include "list.h"
#include "xmalloc.h"
#include "rozofs.h"
#include "volume.h"
#include <pthread.h>
#include "storageclt.h"

static int volume_storage_compare(list_t * l1, list_t *l2) {
    volume_storage_t *e1 = list_entry(l1, volume_storage_t, list);
    volume_storage_t *e2 = list_entry(l2, volume_storage_t, list);

    // online server takes priority
    if ((!e1->status && e2->status) || (e1->status && !e2->status)) {
        return (e2->status - e1->status);
    }
    return e2->stat.free - e1->stat.free;
}

static int cluster_compare_capacity(list_t *l1, list_t *l2) {
    cluster_t *e1 = list_entry(l1, cluster_t, list);
    cluster_t *e2 = list_entry(l2, cluster_t, list);
    return e1->free < e2->free;
}

void volume_storage_initialize(volume_storage_t * vs, uint16_t sid,
        const char *hostname) {
    DEBUG_FUNCTION;

    vs->sid = sid;
    strcpy(vs->host, hostname);
    vs->stat.free = 0;
    vs->stat.size = 0;
    vs->status = 0;
    list_init(&vs->list);
}

void volume_storage_release(volume_storage_t *vs) {
    DEBUG_FUNCTION;
    return;
}

void cluster_initialize(cluster_t *cluster, cid_t cid, uint64_t size,
        uint64_t free) {
    DEBUG_FUNCTION;
    cluster->cid = cid;
    cluster->size = size;
    cluster->free = free;
    list_init(&cluster->storages);
}

// assume volume_storage had been properly allocated
void cluster_release(cluster_t *cluster) {
    DEBUG_FUNCTION;
    list_t *p, *q;

    list_for_each_forward_safe(p, q, &cluster->storages) {
        volume_storage_t *entry = list_entry(p, volume_storage_t, list);
        list_remove(p);
        volume_storage_release(entry);
        free(entry);
    }
}

int volume_initialize(volume_t *volume, vid_t vid) {
    int status = -1;
    DEBUG_FUNCTION;
    volume->vid = vid;
    list_init(&volume->clusters);
    if (pthread_rwlock_init(&volume->lock, NULL) != 0) {
        goto out;
    }
    status = 0;
out:
    return status;
}

void volume_release(volume_t *volume) {
    list_t *p, *q;
    DEBUG_FUNCTION;
    list_for_each_forward_safe(p, q, &volume->clusters) {
        cluster_t *entry = list_entry(p, cluster_t, list);
        list_remove(p);
        cluster_release(entry);
        free(entry);
    }
    if ((errno = pthread_rwlock_destroy(&volume->lock)) != 0) {
        severe("can't release volume lock: %s", strerror(errno));
    }
}

int volume_safe_copy(volume_t *to, volume_t *from) {
    list_t *p, *q;

    if ((errno = pthread_rwlock_rdlock(&from->lock)) != 0) {
        severe("can't lock volume: %d", from->vid);
        goto error;
    }

    if ((errno = pthread_rwlock_wrlock(&to->lock)) != 0) {
        severe("can't lock volume: %d", to->vid);
        goto error;
    }

    list_for_each_forward_safe(p, q, &to->clusters) {
        cluster_t *entry = list_entry(p, cluster_t, list);
        list_remove(p);
        cluster_release(entry);
        free(entry);
    }

    to->vid = from->vid;
    list_for_each_forward(p, &from->clusters) {
        cluster_t *to_cluster = xmalloc(sizeof(cluster_t));
        cluster_t *from_cluster = list_entry(p, cluster_t, list);
        cluster_initialize(to_cluster, from_cluster->cid, from_cluster->size,
                from_cluster->free);
        list_for_each_forward(q, &from_cluster->storages) {
            volume_storage_t *from_storage = list_entry(q, volume_storage_t, list);
            volume_storage_t *to_storage = xmalloc(sizeof(volume_storage_t));
            volume_storage_initialize(to_storage, from_storage->sid, from_storage->host);
            to_storage->stat = from_storage->stat;
            to_storage->status = from_storage->status;
            list_push_front(&to_cluster->storages, &to_storage->list);
        }
        list_push_front(&to->clusters, &to_cluster->list);
    }

    if ((errno = pthread_rwlock_unlock(&from->lock)) != 0) {
        severe("can't unlock volume: %d", from->vid);
        goto error;
    }

    if ((errno = pthread_rwlock_unlock(&to->lock)) != 0) {
        severe("can't unlock volume: %d", to->vid);
        goto error;
    }

    return 0;
error:
    // Best effort to release locks
    pthread_rwlock_unlock(&from->lock);
    pthread_rwlock_unlock(&to->lock);
    return -1;

}

void volume_balance(volume_t *volume) {
    list_t *p, *q;
    volume_t clone;
    DEBUG_FUNCTION;

    // create a working copy
    if (volume_initialize(&clone, 0) != 0) {
        severe("can't initialize clone volume: %d", volume->vid);
        return;
    }

    if (volume_safe_copy(&clone, volume) != 0) {
        severe("can't clone volume: %d", volume->vid);
        return;
    }

    // work on the clone
    // try to join each storage server & stat it
    list_for_each_forward(p, &clone.clusters) {
        cluster_t *cluster = list_entry(p, cluster_t, list);

        cluster->free = 0;
        cluster->size = 0;

        list_for_each_forward(q, &cluster->storages) {
            volume_storage_t *vs = list_entry(q, volume_storage_t, list);
            storageclt_t sclt;
            strcpy(sclt.host, vs->host);
            sclt.sid = vs->sid;

            if (storageclt_initialize(&sclt) != 0) {
                warning("failed to join: %s,  %s", vs->host, strerror(errno));
                vs->status = 0;
            } else {
                if (storageclt_stat(&sclt, &vs->stat) != 0) {
                    warning("failed to stat (sid: %d, host: %s)", vs->sid, vs->host);
                    vs->status = 0;
                } else {
                    vs->status = 1;
                }
            }

            cluster->free += vs->stat.free;
            cluster->size += vs->stat.size;

            storageclt_release(&sclt);
        }
    }

    // sort the clone
    // no need to lock the volume since it's a local only volume
    list_for_each_forward(p, &clone.clusters) {
        cluster_t *cluster = list_entry(p, cluster_t, list);
        list_sort(&cluster->storages, volume_storage_compare);
    }
    list_sort(&clone.clusters, cluster_compare_capacity);

    // move the result back to our volume
    if (volume_safe_copy(volume, &clone) != 0) {
        severe("can't clone volume: %d", volume->vid);
        return;
    }
}

/*
char *lookup_volume_storage(volume_t *volume, sid_t sid, char *host) {
    list_t *p, *q;
    DEBUG_FUNCTION;

    if ((errno = pthread_rwlock_rdlock(&volume->lock)) != 0) {
        severe("can't lock volume: %d", volume->vid);
        goto out;
    }
    list_for_each_forward(p, &volume->clusters) {
        cluster_t *cluster = list_entry(p, cluster_t, list);
        list_for_each_forward(q, &cluster->storages) {
            volume_storage_t *vs = list_entry(q, volume_storage_t, list);
            if (sid == vs->sid) {
                strcpy(host, vs->host);
                break;
            }
        }
    }
    if ((errno = pthread_rwlock_unlock(&volume->lock)) != 0) {
        severe("can't unlock volume: %d", volume->vid);
    }

out:
    return host;
}
*/

// what if a cluster is < rozofs safe
static int cluster_distribute(cluster_t *cluster, uint16_t *sids) {
    list_t *p;
    int status = -1;
    int ms_found = 0;
    int ms_ok = 0;
    DEBUG_FUNCTION;

    list_for_each_forward(p, &cluster->storages) {
        volume_storage_t *vs = list_entry(p, volume_storage_t, list);
        if (vs->status != 0 || vs->stat.free != 0)
            ms_ok++;
        sids[ms_found++] = vs->sid;

        // When creating a file we must be sure to have rozofs_safe servers
        // and have at least rozofs_server available for writing
        if (ms_found == rozofs_safe && ms_ok >= rozofs_forward) {
            status = 0;
            break;
        }
    }
    return status;
}

int volume_distribute(volume_t *volume, uint16_t *cid, uint16_t *sids) {
    list_t *p;
    DEBUG_FUNCTION;

    if ((errno = pthread_rwlock_rdlock(&volume->lock)) != 0) {
        warning("can't lock volume %d.", volume->vid);
        goto out;
    }
    errno = ENOSPC;
    list_for_each_forward(p, &volume->clusters) {
        cluster_t *cluster = list_entry(p, cluster_t, list);
        if (cluster_distribute(cluster, sids) == 0) {
            *cid = cluster->cid;
            errno = 0;
            break;
        }
    }
    if ((errno = pthread_rwlock_unlock(&volume->lock)) != 0) {
        warning("can't unlock volume %d.", volume->vid);
        goto out;
    }
out:
    return errno == 0 ? 0 : -1;
}

void volume_stat(volume_t *volume, volume_stat_t *stat) {
    list_t *p;
    DEBUG_FUNCTION;

    stat->bsize = ROZOFS_BSIZE;
    stat->bfree = 0;

    if ((errno = pthread_rwlock_rdlock(&volume->lock)) != 0) {
        warning("can't lock volume %d.", volume->vid);
    }

    list_for_each_forward(p, &volume->clusters) {
        stat->bfree += list_entry(p, cluster_t, list)->free / ROZOFS_BSIZE;
    }

    if ((errno = pthread_rwlock_unlock(&volume->lock)) != 0) {
        warning("can't unlock volume %d.", volume->vid);
    }

    stat->bfree = (long double) stat->bfree / ((double) rozofs_forward /
            (double) rozofs_inverse);
}
