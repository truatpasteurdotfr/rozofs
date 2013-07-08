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

/*
 * monitoring.h
 *
 *  Created on: 26 oct. 2012
 *      Author: pierre
 */

#ifndef _MONITORING_H
#define _MONITORING_H

/**
 * Initialize a monitor tcp server.
 *
 * @param test: ?
 * @param port: the listening port.
 *
 * @return: 0 on success -1 otherwise (warning errno is not set)
 */
int monitoring_initialize(uint32_t test, uint16_t port);

#endif
