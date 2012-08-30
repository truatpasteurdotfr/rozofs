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

#ifndef _DIST_H
#define _DIST_H

#include <stdint.h>
#include "list.h"

/** API distribution management functions
 *
 * distribution are used to map projection to storage server
 * this api does not managed storage server set (done in export)
 * but rather indicates if index of storage server in a set managed
 * elsewhere is in use or not.
 */


/** dist_t is a simple 16 bits bitmap (1 if server in use otherwise 0) */
typedef uint16_t dist_t;

/** check if a bit is set on a dist_t
 *
 * @param d: dist_t used
 * @param b: bit index
 */
#define dist_is_set(d, b) (d & (1L << b % 16L) ? 1 : 0)

/** set a bit to true on a dist_t
 *
 * @param d: dist_t used
 * @param b: bit index
 */
#define dist_set_true(d, b) (d |= (1L << b % 16L))

/** set a bit to false on a dist_t
 *
 * @param d: dist_t used
 * @param b: bit index
 */
#define dist_set_false(d, b) (d &= ~(1L << b % 16L))

/** set a bit to true or false on a dist_t according to given value
 *
 * @param d: dist_t used
 * @param b: bit index
 * @param v: value (0 bit is set to false otherwise bit is set to 1)
 */
#define dist_set_value(d, b, v) ((v) ? \
		dist_set_true(d, b) : dist_set_false(d, b))

#endif
