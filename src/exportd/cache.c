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

#include <sys/param.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <uuid/uuid.h>
#include <errno.h>

#include <rozofs/common/xmalloc.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/epproto.h>

#include "cache.h"


DECLARE_PROFILING(epp_profiler_t);

/**
 * hashing function used to find lv2 entry in the cache
 */
static inline uint32_t lv2_hash(void *key) {
    uint32_t hash = 0;
    uint8_t *c;

    for (c = key; c != key + 16; c++)
        hash = *c + (hash << 6) + (hash << 16) - hash;
    return hash;
}

static inline int lv2_cmp(void *k1, void *k2) {
    return uuid_compare(k1, k2);
}

#define LV2_BUKETS 128
#define LV2_MAX_ENTRIES 256

char * lv2_cache_display(lv2_cache_t *cache, char * pChar) {

  pChar += sprintf(pChar, "lv2 attributes cache : current/max %u/%u\n",cache->size, cache->max);
  pChar += sprintf(pChar, "hit %llu / miss %llu\n",cache->hit, cache->miss);
  pChar += sprintf(pChar, "entry size %u - current size %u - maximum size %u\n", 
                   sizeof(lv2_entry_t), sizeof(lv2_entry_t)*cache->size, sizeof(lv2_entry_t)*cache->max); 
  return pChar;		   
}

void lv2_cache_initialize(lv2_cache_t *cache) {
    cache->max = LV2_MAX_ENTRIES;
    cache->size = 0;
    cache->hit  = 0;
    cache->miss = 0;
    list_init(&cache->entries);
    htable_initialize(&cache->htable, LV2_BUKETS, lv2_hash, lv2_cmp);
}

void lv2_cache_release(lv2_cache_t *cache) {
    list_t *p, *q;

    list_for_each_forward_safe(p, q, &cache->entries) {
        lv2_entry_t *entry = list_entry(p, lv2_entry_t, list);
        htable_del(&cache->htable, entry->attributes.fid);
        if (S_ISDIR(entry->attributes.mode)) {
            mdir_close(&entry->container.mdir);
        } else if (S_ISREG(entry->attributes.mode)) {
            mreg_close(&entry->container.mreg);
        } else if (S_ISLNK(entry->attributes.mode)) {
            mslnk_close(&entry->container.mslnk);
        }
        mattr_release(&entry->attributes);
        list_remove(&entry->list);
        free(entry);
    }
}

lv2_entry_t *lv2_cache_put(lv2_cache_t *cache, fid_t fid, const char *path) {
    lv2_entry_t *entry;
    struct stat st;

    START_PROFILING(lv2_cache_put);

    // maybe already cached.
    if ((entry = htable_get(&cache->htable, fid)) != 0) {
        goto out;
    }

    if (stat(path, &st) != 0) {
        goto error;
    }

    entry = xmalloc(sizeof(lv2_entry_t));

    if (S_ISDIR(st.st_mode)) {
        if (mdir_open(&entry->container.mdir, path) != 0) {
            goto error;
        }
        if (mdir_read_attributes(&entry->container.mdir, &entry->attributes) != 0) {
            goto error;
        }
    } else if (S_ISREG(st.st_mode)) {
        if (mreg_open(&entry->container.mreg, path) != 0) {
            goto error;
        }
        if (mreg_read_attributes(&entry->container.mreg, &entry->attributes) != 0) {
            goto error;
        }
    } else if (S_ISLNK(st.st_mode)) {
        if (mslnk_open(&entry->container.mslnk, path) != 0) {
            goto error;
        }
        if (mslnk_read_attributes(&entry->container.mslnk, &entry->attributes) != 0) {
            goto error;
        }
    } else {
        errno = ENOTSUP;
        goto error;
    }

    list_push_front(&cache->entries, &entry->list);
    htable_put(&cache->htable, entry->attributes.fid, entry);
    
    if (cache->size++ >= cache->max) { // remove the lru
        lv2_entry_t *lru = list_entry(cache->entries.prev, lv2_entry_t, list);
        if (S_ISDIR(lru->attributes.mode)) {
            mdir_close(&lru->container.mdir);
        } else if (S_ISREG(lru->attributes.mode)) {
            mreg_close(&lru->container.mreg);
        } else if (S_ISLNK(lru->attributes.mode)) {
            mslnk_close(&lru->container.mslnk);
        } else {
            errno = ENOTSUP;
            goto error;
        }
        htable_del(&cache->htable, lru->attributes.fid);
        mattr_release(&lru->attributes);
        list_remove(&lru->list);
        free(lru);
        cache->size--;
    }

    goto out;
error:
    if (entry) {
        if (S_ISDIR(st.st_mode)) {
            mdir_close(&entry->container.mdir);
        } else if (S_ISREG((st.st_mode))) {
            mreg_close(&entry->container.mreg);
        } else if (S_ISLNK(st.st_mode)) {
            mslnk_close(&entry->container.mslnk);
        }
        free(entry);
        entry = 0;
    }
out:
    STOP_PROFILING(lv2_cache_put);
    return entry;
}

lv2_entry_t *lv2_cache_get(lv2_cache_t *cache, fid_t fid) {
    lv2_entry_t *entry = 0;

    START_PROFILING(lv2_cache_get);

    if ((entry = htable_get(&cache->htable, fid)) != 0) {
        // push the lru
        list_remove(&entry->list);
        list_push_front(&cache->entries, &entry->list);
	cache->hit++;
    }
    else {
      cache->miss++;
    }

    STOP_PROFILING(lv2_cache_get);
    return entry;
}

void lv2_cache_del(lv2_cache_t *cache, fid_t fid) {
    lv2_entry_t *entry = 0;
    START_PROFILING(lv2_cache_del);

    if ((entry = htable_del(&cache->htable, fid)) != 0) {
        if (S_ISDIR(entry->attributes.mode)) {
            mdir_close(&entry->container.mdir);
        } else if (S_ISREG(entry->attributes.mode)) {
            mreg_close(&entry->container.mreg);
        } else if (S_ISLNK(entry->attributes.mode)) {
            mslnk_close(&entry->container.mslnk);
        }
        mattr_release(&entry->attributes);
        list_remove(&entry->list);
        free(entry);
        cache->size--;
    }
    STOP_PROFILING(lv2_cache_del);
}
