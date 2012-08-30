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

#ifndef _SLNK_H
#define _SLNK_H

#include "rozofs.h"
#include "mattr.h"

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

#endif
