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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h> 
#include <sys/wait.h>

#include <rozofs/common/list.h>
#include <rozofs/common/htable.h>
#include <rozofs/rozofs.h>
#include <rozofs/rozofs_srv.h>
#include <rozofs/rpc/rpcclt.h>
#include <rozofs/common/profile.h>

#include "sconfig.h"
#include "storage.h"
#include "rbs_sclient.h"
#include "rbs_eclient.h"
#include "rbs.h"
#include "storage.h"
 
extern char              * fid2rebuild_string;
extern char                command[];
extern uint64_t            rb_fid_table_count;
extern int   cid;
extern int   sid;
extern int   rbs_device_number;
extern uint64_t  total_file_count;
extern int   cid_table[];
extern int   sid_table[];
extern int   nb_sid;

