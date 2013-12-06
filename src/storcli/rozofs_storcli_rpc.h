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

#ifndef _ROZOFS_STORCLI_RPC_H
#define _ROZOFS_STORCLI_RPC_H 1

#include "config.h"
#include <sys/types.h>
#include <rpc/rpc.h>
#ifndef WIN32
#include <netinet/in.h>
#endif				       /* WIN32 */

#if HAVE_XDR_U_INT64_T == 1
#define xdr_uint64_t xdr_u_int64_t
#undef HAVE_XDR_UINT64_T
#define HAVE_XDR_UINT64_T 1
#endif

#include <rozofs/rpc/rozofs_rpc_util.h>



enum storage_msg_type {
	STORAGED_CALL=0,
	STORAGED_REPLY=1
};


/*
*________________________________________________________
*/
/**
  That API returns the length of a successful RPC reply header excluding the length header
  of the RPC message
  
*/
#define rozofs_storcli_get_min_rpc_reply_hdr_len rozofs_rpc_get_min_rpc_reply_hdr_len


/*
*________________________________________________________
*/
/**
* The purpose of that procudure is to return the pointer to the first available byte after 
  the rpc header 

  @param p : pointer to the first byte that follows the size of the rpc message
  @param len_p : pointer to an array where the system with return the length of the RPC header array

  @retval <>NULL: pointer to the first NFS message available byte
  @retval == NULL: error
  
*/
#define rozofs_storcli_set_ptr_on_nfs_call_msg rozofs_rpc_set_ptr_on_first_byte_after_rpc_header




#endif
