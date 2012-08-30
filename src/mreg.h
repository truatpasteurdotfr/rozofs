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

#ifndef _MREG_H
#define _MREG_H

#include "rozofs.h"
#include "mattr.h"
#include "dist.h"

/** API mreg management functions.
 * TODO
 */

// for now attributes only
// might be use for dist caching later
typedef struct mreg {
    int fdattrs;    ///< attributes file descriptor
} mreg_t;

/** open the mreg
 *
 * @param mreg: the mreg to open
 * @param path: the corresponding path
 */
int mreg_open(mreg_t *mreg, const char *path);

/** close the mreg
 *
 * @param mreg: the mreg to close
 */
void mreg_close(mreg_t *mreg);

/** read attributes from the lv2 regular file.
 *
 * @param fd: an opened file descriptor to read from.
 *
 * @return: 0 on success -1 otherwise (errno is set).
 */
int mreg_read_attributes(mreg_t *mreg, mattr_t *mattr);

/** write attributes to the underlying regular file.
 *
 * @param fd: an opened file descriptor to write to.
 *
 * @return: 0 on success -1 otherwise (errno is set).
 */
int mreg_write_attributes(mreg_t *mreg, mattr_t *mattr);

/** read distribution from the underlying regular file.
 * *
 * @param fd: an opened file descriptor to read from.
 * @param bid: first block to read.
 * @param n: number of blocks to read.
 * @param dist: pointer on which dist is read.
 *
 * @return: 0 on success -1 otherwise (errno is set).
 */
int mreg_read_dist(mreg_t *mreg, bid_t bid, uint32_t n, dist_t *dist);

/** write distribution to the underlying regular file.
 *
 * @param fd: an opened file descriptor to write on.
 * @param bid: first block to write.
 * @param n: number of blocks to write.
 * @param dist: pointer from which dist is write.
 *
 * @return: 0 on success -1 otherwise (errno is set).
 */
int mreg_write_dist(mreg_t *mreg, bid_t bid, uint32_t n, dist_t *dist);

#endif
