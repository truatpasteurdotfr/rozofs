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

#ifndef _CACHE_H
#define _CACHE_H

#include <rozofs/rozofs.h>
#include <rozofs/common/list.h>
#include <rozofs/common/htable.h>
#include <rozofs/common/mattr.h>

#include "mreg.h"
#include "mdir.h"
#include "mslnk.h"

/** API lv2 cache management functions.
 *
 * lv2 cache is common to several exports to take care of max fd opened.
 */

/** lv2 entry cached */
typedef struct lv2_entry {
    mattr_t attributes; ///< attributes of this entry
    union {
        mreg_t mreg;    ///< regular file
        mdir_t mdir;    ///< directory
        mslnk_t mslnk;  ///< symlink
    } container;
    list_t list;        ///< list used by cache
} lv2_entry_t;

/** lv2 cache
 *
 * used to keep track of open file descriptors and corresponding attributes
 */
typedef struct lv2_cache {
    int max;            ///< max entries in the cache
    int size;           ///< current number of entries
    list_t entries;     ///< entries cached
    htable_t htable;    ///< entries hashing
} lv2_cache_t;


/** initialize a new empty lv2 cache
 *
 * @param cache: the cache to initialize
 */
void lv2_cache_initialize(lv2_cache_t *cache);

/** release a lv2 cache
 *
 * @param cache: the cache to release
 */
void lv2_cache_release(lv2_cache_t *cache);

/** put an entry from the given lv2 path
 *
 * @param cache: the cache to search in
 * @param fid: fid to be cached
 * @param path: path to the underlying file
 *
 * @return: a pointer to the lv2_entry or null on error (errno is set)
 */
lv2_entry_t *lv2_cache_put(lv2_cache_t *cache, fid_t fid, const char *path);

/** get an entry from the given lv2 cache
 *
 * if the entry is not cached, it will be retrieved from the underlying
 * file system.
 *
 * @param export: the export we rely on
 * @param cache: the cache to search in
 * @param fid: the fid we are looking for
 *
 * @return: a pointer to the lv2_entry or null on error (errno is set)
 */
lv2_entry_t *lv2_cache_get(lv2_cache_t *cache, fid_t fid);

/** delete an entry from the given lv2 cache
 *
 * has no effect if the entry is not cached, otherwise entry is removed
 * and freed
 *
 * @param export: the export we rely on
 * @param cache: the cache to search in
 * @param fid: the fid we are looking for
 *
 */
void lv2_cache_del(lv2_cache_t *cache, fid_t fid);

#endif
