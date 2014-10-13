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

 
#ifndef RBS_MONITOR_H
#define RBS_MONITOR_H

#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/rozofs_srv.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/spproto.h>
#include <rozofs/rpc/sproto.h>
#include <rozofs/core/uma_dbg_api.h>

/*
**____________________________________________________
** Purge excedent files
*/
int rbs_monitor_purge(void) ;
/*
**____________________________________________________
** Create or re-create the monitoring file for this rebuild process
*/
int rbs_monitor_update(char * rebuild_status, int run_loop) ;
#endif
