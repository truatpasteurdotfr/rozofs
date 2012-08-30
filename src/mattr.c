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

#include <string.h>

#include "mattr.h"
#include "xmalloc.h"

void mattr_initialize(mattr_t *mattr) {
    check_memory(mattr);

    //mattr->fid XXX fid not initialized.
    mattr->cid = UINT16_MAX;
    memset(mattr->sids, 0, ROZOFS_SAFE_MAX);
}

void mattr_release(mattr_t *mattr) {
    check_memory(mattr);

    //mattr->fid XXX fid not initialized.
    mattr->cid = UINT16_MAX;
    memset(mattr->sids, 0, ROZOFS_SAFE_MAX);
}
