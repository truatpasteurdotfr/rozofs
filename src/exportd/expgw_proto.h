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
 
 #ifndef EXPGW_PROTO_H
#define EXPGW_PROTO_H


#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <pthread.h>

#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/epproto.h>
#include <rozofs/rpc/eproto.h>

#include "expgw_export.h"
#include "expgw_fid_cache.h"
#include "expgw_attr_cache.h"
#include "export.h"
#include "volume.h"
#include "exportd.h"
/*
**______________________________________________________________________________
*/
/**
*  Export lookup

  That API attempts to find out the fid associated with a parent fid and a name
*/
void expgw_lookup_1_svc(epgw_lookup_arg_t * arg, expgw_ctx_t *req_ctx_p);
void expgw_getattr_1_svc(epgw_mfile_arg_t * arg, expgw_ctx_t *req_ctx_p); 
void expgw_setattr_1_svc(epgw_mfile_arg_t * arg, expgw_ctx_t *req_ctx_p); 
void expgw_mknod_1_svc(epgw_mknod_arg_t * arg, expgw_ctx_t *req_ctx_p); 
void expgw_mkdir_1_svc(epgw_mkdir_arg_t * arg, expgw_ctx_t *req_ctx_p);
void expgw_rmdir_1_svc(epgw_rmdir_arg_t * arg, expgw_ctx_t *req_ctx_p) ;
void expgw_unlink_1_svc(epgw_unlink_arg_t * arg, expgw_ctx_t *req_ctx_p); 
void expgw_symlink_1_svc(epgw_symlink_arg_t * arg, expgw_ctx_t *req_ctx_p);
void expgw_link_1_svc(epgw_link_arg_t * arg, expgw_ctx_t *req_ctx_p);
void expgw_write_block_1_svc(epgw_write_block_arg_t * arg, expgw_ctx_t *req_ctx_p); 



#ifdef __cplusplus
}
#endif /*__cplusplus */


#endif


