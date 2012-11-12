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

#ifndef _MREG_H
#define _MREG_H

#include <rozofs/rozofs.h>
#include <rozofs/common/mattr.h>
#include <rozofs/common/dist.h>

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

/** set an extended attribute value for a lv2 regular file.
 *
 * @param mreg: the mreg.
 * @param name: the extended attribute name.
 * @param value: the value of this extended attribute.
 * @param size: the size of a buffer to hold the value associated
 *  with this extended attribute.
 * @param flags: parameter can be used to refine the semantics of the operation.
 * 
 * @return: On success, zero is returned.  On failure, -1 is returned.
 */
int mreg_set_xattr(mreg_t *mreg, const char *name, const void *value, size_t size, int flags);

/** retrieve an extended attribute value from the lv2 regular file.
 *
 * @param mreg: the mreg to read from.
 * @param name: the extended attribute name.
 * @param value: the value of this extended attribute.
 * @param size: the size of a buffer to hold the value associated
 *  with this extended attribute.
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
ssize_t mreg_get_xattr(mreg_t *mreg, const char *name, void *value, size_t size);

/** remove an extended attribute from the lv2 regular file.
 *
 * @param mreg: the mreg for this regular file.
 * @param name: the extended attribute name.
 * 
 * @return: On success, zero is returned.  On failure, -1 is returned.
 */
int mreg_remove_xattr(mreg_t *mreg, const char *name);

/** list extended attribute names from the lv2 regular file.
 *
 * @param mreg: the mreg for this regular file.
 * @param list: list of extended attribute names associated with this file.
 * @param size: the size of a buffer to hold the list of extended attributes.
 * 
 * @return: On success, the size of the extended attribute name list.
 * On failure, -1 is returned and errno is set appropriately.
 */
ssize_t mreg_list_xattr(mreg_t *mreg, char *list, size_t size);

#endif
