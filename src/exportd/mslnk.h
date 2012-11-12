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

#ifndef _SLNK_H
#define _SLNK_H

#include <rozofs/rozofs.h>
#include <rozofs/common/mattr.h>

/** API mslnk management functions.
 * TODO
 */

// for now attributes only
// might be use for dist caching later
typedef struct mslnk {
    int fdattrs;    ///< attributes file descriptor
} mslnk_t;

/** open the mslnk
 *
 * @param mslnk: the mslnk to open
 * @param path: the corresponding path
 */
int mslnk_open(mslnk_t *mslnk, const char *path);

/** close the mslnk
 *
 * @param mslnk: the mslnk to close
 */
void mslnk_close(mslnk_t *mslnk);

/** read attributes from the lv2 regular file.
 *
 * @param mslnk: the msnlk.
 *
 * @return: 0 on success -1 otherwise (errno is set).
 */
int mslnk_read_attributes(mslnk_t *mslnk, mattr_t *mattr);

/** write attributes to the underlying regular file.
 *
 * @param mslnk: the mslnk.
 *
 * @return: 0 on success -1 otherwise (errno is set).
 */
int mslnk_write_attributes(mslnk_t *mslnk, mattr_t *mattr);

/** read link from the underlying regular file.
 * *
 * @param mslnk: the mslnk.
 * @param link: pointer on which the link is read.
 *
 * @return: 0 on success -1 otherwise (errno is set).
 */
int mslnk_read_link(mslnk_t *mslnk, char *link);

/** write link to the underlying regular file.
 *
 * @param mslnk: the mslnk.
 * @param link: pointer from which link is write.
 *
 * @return: 0 on success -1 otherwise (errno is set).
 */
int mslnk_write_link(mslnk_t *mslnk, char *link);

/** set an extended attribute value for a lv2 link.
 *
 * @param mslnk: the link.
 * @param name: the extended attribute name.
 * @param value: the value of this extended attribute.
 * @param size: the size of a buffer to hold the value associated
 *  with this extended attribute.
 * @param flags: parameter can be used to refine the semantics of the operation.
 * 
 * @return: On success, zero is returned.  On failure, -1 is returned.
 */
int mslnk_set_xattr(mslnk_t *mslnk, const char *name, const void *value, size_t size, int flags);

/** retrieve an extended attribute value from the lv2 link.
 *
 * @param mslnk: the mslnk to read from.
 * @param name: the extended attribute name.
 * @param value: the value of this extended attribute.
 * @param size: the size of a buffer to hold the value associated
 *  with this extended attribute.
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
ssize_t mslnk_get_xattr(mslnk_t *mslnk, const char *name, void *value, size_t size);

/** remove an extended attribute from the lv2 link.
 *
 * @param mslnk: the mdir for this link.
 * @param name: the extended attribute name.
 * 
 * @return: On success, zero is returned.  On failure, -1 is returned.
 */
int mslnk_remove_xattr(mslnk_t *mslnk, const char *name);

/** list extended attribute names from the lv2 link.
 *
 * @param mslnk: the mslnk_t for this link.
 * @param list: list of extended attribute names associated with this link.
 * @param size: the size of a buffer to hold the list of extended attributes.
 * 
 * @return: On success, the size of the extended attribute name list.
 * On failure, -1 is returned and errno is set appropriately.
 */
ssize_t mslnk_list_xattr(mslnk_t *mslnk, char *list, size_t size);

#endif
