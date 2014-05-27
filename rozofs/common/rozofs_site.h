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
#ifndef _ROZOFS_SITE_H
#define _ROZOFS_SITE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include "config.h"
#include "rozofs_site.h"

/*
 *_______________________________________________________________________
 */
/**
*  interpret the rozofs_site file

   @param : none
   @param[out]: pointer to the array where hash value is returned

   @retval >=0 : local site number 0 or 1
   @retval -1 on error (see errno for details)
*/
int rozofs_get_local_site();

#endif
