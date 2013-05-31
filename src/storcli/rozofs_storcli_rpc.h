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

typedef struct rozofs_rpc_common_hdr
{
//   uint32_t   msg_sz;    /**< size of the RPC message including all header  */
   uint32_t   xid;    /**< transaction identifier  */
   uint32_t   direction;  /**< 1: reply; 0: call   */
}  rozofs_rpc_common_hdr_t;

/**
* common header to RPC call
*/
typedef struct rozofs_rpc_call_hdr
{
   rozofs_rpc_common_hdr_t  hdr;
   uint32_t rpcvers;  /**< version number of rozofs RPC Must be 1 */
   uint32_t prog;     /**< reference of the program to be called  */
   uint32_t vers;     /**< version of the program                 */
   uint32_t proc;     /**< reference of the procedure to call     */
} rozofs_rpc_call_hdr_t;


typedef struct rozofs_rpc_call_hdr_with_sz
{
   uint32_t   msg_sz;    /**< size of the RPC message including all header  */
   rozofs_rpc_common_hdr_t  hdr;
   uint32_t rpcvers;  /**< version number of rozofs RPC Must be 1 */
   uint32_t prog;     /**< reference of the program to be called  */
   uint32_t vers;     /**< version of the program                 */
   uint32_t proc;     /**< reference of the procedure to call     */
} rozofs_rpc_call_hdr_with_sz_t;

/**
* swap the RPC call header to host order
*/
#define ROZOFS_GET_UINT32(val) val = ntohl(val);
#define ROZOFS_GET_UINT16(val) val = ntohs(val);

#define ROZOFS_SWAP_uint16(x) \
    x =((uint16_t)((((uint16_t)(x) & 0xff00) >> 8) | \
                (((uint16_t)(x) & 0x00ff) << 8)))

#define ROZOFS_SWAP_uint32(x) \
    x =((uint32_t)((((uint32_t)(x) & 0xff000000) >> 24) | \
                (((uint32_t)(x) & 0x00ff0000) >>  8) | \
                (((uint32_t)(x) & 0x0000ff00) <<  8) | \
                (((uint32_t)(x) & 0x000000ff) << 24)))

#define ROZOFS_SWAP_uint64(x) \
    x =((uint64_t)((((uint64_t)(x) & 0xff00000000000000ULL) >> 56) | \
                (((uint64_t)(x) & 0x00ff000000000000ULL) >> 40) | \
                (((uint64_t)(x) & 0x0000ff0000000000ULL) >> 24) | \
                (((uint64_t)(x) & 0x000000ff00000000ULL) >>  8) | \
                (((uint64_t)(x) & 0x00000000ff000000ULL) <<  8) | \
                (((uint64_t)(x) & 0x0000000000ff0000ULL) << 24) | \
                (((uint64_t)(x) & 0x000000000000ff00ULL) << 40) | \
                (((uint64_t)(x) & 0x00000000000000ffULL) << 56)))


enum storage_msg_type {
	STORAGED_CALL=0,
	STORAGED_REPLY=1
};



static inline void scv_call_hdr_ntoh(rozofs_rpc_call_hdr_t *hdr)
{
  ROZOFS_GET_UINT32(hdr->hdr.xid);
  ROZOFS_GET_UINT32(hdr->hdr.direction);
  ROZOFS_GET_UINT32(hdr->rpcvers);
  ROZOFS_GET_UINT32(hdr->prog);
  ROZOFS_GET_UINT32(hdr->vers);
  ROZOFS_GET_UINT32(hdr->proc);

}


static inline void scv_common_hdr_ntoh(rozofs_rpc_common_hdr_t *hdr)
{
  ROZOFS_GET_UINT32(hdr->xid);
  ROZOFS_GET_UINT32(hdr->direction);

}
/*
*________________________________________________________
*/
/**
  That API returns the length of a successful RPC reply header excluding the length header
  of the RPC message
  
*/
static inline int rozofs_storcli_get_min_rpc_reply_hdr_len()
{
  /*
  ** the rpc reply header includes the following field
    xid       : 4 bytes
    direction : 4 bytes
    status    : 4 bytes
    verifier_flavor    : 4 bytes
    verifier_len    : 4 bytes
    status    : 4 bytes
 */
 return   (6*sizeof(uint32_t));

}

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
static inline  uint8_t *rozofs_storcli_set_ptr_on_nfs_call_msg(char *p,uint32_t *len_p)
{

  rozofs_rpc_call_hdr_t hdr;
  uint32_t credential_len;
  uint32_t verifier_len;
  uint8_t  *pbuf;
  uint32_t *p32; 
  /*
  ** copy the initial RPC header message in a temporary area in order to move
  ** it to the host format. We do not do it on the original message since
  ** that one might be sent on the line afterwards (to avoid little/bigg endian swap issue)
  */
  memcpy(&hdr,p,sizeof(rozofs_rpc_call_hdr_t));  
  scv_call_hdr_ntoh(&hdr);
  /*
  ** set the buffer pointer to the top of the rpc message
  ** and analyse the RPC header in order to make the buffer pointing on the first available
  ** data of the NFS message (RPC message without RPC header stuff)
  */
  pbuf = (uint8_t*)p;
  pbuf +=sizeof(rozofs_rpc_call_hdr_t);
  pbuf +=sizeof(uint32_t); // skip credential flavor
  p32 = (uint32_t*)pbuf;
  credential_len = ntohl(*p32);
  /*
  ** caution with might need to round up on a 4 bytes boundary
  */
  credential_len = (credential_len-1) / BYTES_PER_XDR_UNIT;
  credential_len = BYTES_PER_XDR_UNIT*(credential_len+1);
  
  pbuf += sizeof(uint32_t)+credential_len;
  pbuf +=sizeof(uint32_t); // skip verifier flavor 
  p32 = (uint32_t*)pbuf;
  verifier_len = ntohl(*p32);
  /*
  ** caution with might need to round up on a 4 bytes boundary
  */
  verifier_len = (verifier_len-1) / BYTES_PER_XDR_UNIT;
  verifier_len = BYTES_PER_XDR_UNIT*(verifier_len+1);
  
  pbuf += sizeof(uint32_t)+verifier_len;
  *len_p =(int)((char*)pbuf - (char*)p);
  return pbuf;
}


/*
*________________________________________________________
*/
/**
*  encode a success response to a rpc reply

  @param  xdrs: pointer to the array where data is encoded 
  @param  proc: user specific encoding procedure
  @param where: user specific parameters
  @param xid: transaction identifier
  
  @retval FALSE : error on encoding
  @retval TRUE: success
 */
static inline bool_t rozofs_encode_rpc_reply(XDR *xdrs,xdrproc_t proc,caddr_t where, uint32_t xid)
{

   struct rpc_msg  rpc_reply;
   
    rpc_reply.rm_xid              = xid;
    rpc_reply.rm_direction        = REPLY;
    rpc_reply.rm_reply.rp_stat    = MSG_ACCEPTED;
    rpc_reply.acpted_rply.ar_stat = SUCCESS;
//    rpc_reply.acpted_rply.ar_verf = NULL;   /* not used */
    rpc_reply.acpted_rply.ar_results.where = where;
    rpc_reply.acpted_rply.ar_results.proc = proc;
    if (rozofs_xdr_replymsg(xdrs,&rpc_reply) != TRUE)
    {
      severe("rpc reply encode error");
      return FALSE;
    }
    return TRUE;   
}


#endif
