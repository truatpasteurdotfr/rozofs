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

#ifndef _STORAGED_H
#define _STORAGED_H

#include <rozofs/rozofs.h>

#include "storage.h"

/** Number of storage processes. */
extern uint8_t storaged_nb_io_processes;

/** Corresponding ports */
extern uint32_t storaged_storage_ports[STORAGE_NODE_PORTS_MAX];
extern uint8_t storaged_nb_ports;

/* public API */

/**
 *  Get a storage with the given cid and sid
 *
 *  @param cid: the cid for the searched storage
 *  @param sid: the sid for the searched storage
 *  @return : the wanted storage_t or 0 if not found (errno is set to EINVAL)
 */
storage_t *storaged_lookup(cid_t cid, sid_t sid);

#endif
