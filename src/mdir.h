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

#ifndef _MDIR_H
#define _MDIR_H

#include "rozofs.h"
#include "mattr.h"

/** API mdir management functions.
 * TODO
 */
#define MDIR_ATTRS_FNAME "attributes"

typedef struct mdir {
    int fdp;        ///< directory file descriptor
    int fdattrs;    ///< attributes file descriptor
    int children;   ///< number of children (excluding . and ..)
    uint8_t dir_vers;     ///< format version of mdirentries
} mdir_t;

/** open the mdir
 *
 * @param mdir: the mdir to open
 * @param path: the corresponding path
 */
int mdir_open(mdir_t *mdir, const char *path);

/** close the mdir
 *
 * @param mdir: the mdir to close
 */
void mdir_close(mdir_t *mdir);

/** read attributes from the underlying directory file
 *
 * @param mdir: an opened lv2 mdir_t
 *
 * @return: 0 on success -1 otherwise (errno is set).
 */
int mdir_read_attributes(mdir_t *mdir, mattr_t *mattr);

/** write attributes to the underlying regular file
 *
 * @param mdir: an opened lv2 mdir_t
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int mdir_write_attributes(mdir_t *mdir, mattr_t *mattr);

#endif
