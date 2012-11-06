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

#ifndef _MATTR_H
#define _MATTR_H

#include <rozofs/rozofs.h>

/** API meta attributes functions.
 */

/** all we need to know about a managed file.
 *
 * attributes fid, cid, sids are rozofs data storage information.
 * others are a used the same way struct stat(2)
 */
typedef struct mattr {
    fid_t fid;                      /**< unique file id */
    cid_t cid;                      /**< cluster id 0 for non regular files */
    sid_t sids[ROZOFS_SAFE_MAX];    /**< sid of storage nodes target (regular file only)*/
    uint32_t mode;                  /**< see stat(2) */
    uint32_t uid;                   /**< see stat(2) */
    uint32_t gid;                   /**< see stat(2) */
    uint16_t nlink;                 /**< see stat(2) */
    uint64_t ctime;                 /**< see stat(2) */
    uint64_t atime;                 /**< see stat(2) */
    uint64_t mtime;                 /**< see stat(2) */
    uint64_t size;                  /**< see stat(2) */
} mattr_t;

/** initialize mattr_t
 *
 * fid is not initialized
 * cid is set to UINT16_MAX (serve to detect unset value)
 * sids is filled with 0
 *
 * @param mattr: the mattr to initialize.
 */
void mattr_initialize(mattr_t *mattr);

/** initialize mattr_t
 *
 * fid is not initialized
 * cid is set to UINT16_MAX (serve to detect unset value)
 * sids is filled with 0
 *
 * @param mattr: the mattr to release.
 */
void mattr_release(mattr_t *mattr);

#endif
