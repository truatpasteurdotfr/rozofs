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

#ifndef RBS_ECLIENT_H
#define RBS_ECLIENT_H

#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

/** Send a request to get the list of storages for this cluster 
 *
 * @param clt: rpc client for the exportd server.
 * @param export_host: exportd server hostname.
 * @param cid: the unique id for this cluster.
 * @param cluster_entries: the list of clusters.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int rbs_get_cluster_list(rpcclt_t * clt, const char *export_host, cid_t cid,
        list_t * cluster_entries);

#endif
