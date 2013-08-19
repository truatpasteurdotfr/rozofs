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
#ifndef RUC_BUFFER_H
#define RUC_BUFFER_H

#include <stdint.h>

#include "ruc_common.h"
#include "ruc_list.h"
//#include "ruc_buffer_api.h"


/*
**  Buffer state
*/
#define BUF_ALLOC 0x55
#define BUF_FREE  0xaa

/*
**  element type
*/
#define BUF_POOL_HEAD 0x22
#define BUF_ELEM      0x33

/*
**  Buffer and buffer pool head structure
**
**    buffer :
**       len : length of the payload
**       ptr : pointer to the payload
**       type : BUF_ELEM
**       state : BUF_ALLOC/BUF_FREE
**       bufCount : max length of the payload
**       usrLen : current length of the user payload
**
**    buffer pool header: (free list)
**       len : length in byte of the buffer pool
**       ptr : pointer to the first payload of the buffer pool
**       type : BUF_POOL_HEAD
**       state : NS
**       bufCount : Number of buffer in the pool.
**       usrLen : Current number of buffer in the pool


*/
#define RUC_BUFFER_USER_ARRAY_SIZE  64  /**< user information opaque to ruc_buffer  */
typedef void (*ruc_pf_buf_t)(void*);
typedef int (*sched_pf_buf_t)(int,void*);
/**
* structure used for traffic shaping
*/
typedef struct _ruc_buf_sched_t
{
   uint32_t            rsp_time;      /**< time of the tcp response */
   uint32_t            disk_time;     /**< time of the disk */
   uint64_t            lbg_id:16;        /**< reference of the load balancing group    */
   uint64_t            filler:48;        /**< for future usage                         */
   sched_pf_buf_t      sched_callBackFct;   /**< called upon buffer release      */

} ruc_buf_shaping_ctx_t;

typedef struct _ruc_buf_t
{
   ruc_obj_desc_t    obj;          /**< link used for queueing the buffer on any kind of queue */
   uint32_t            len;          /**< max length of the payload     */
   uint8_t             *ptr;         /**< pointer to the payload of the buffer  */
   void *              opaque_ref;    /**< opaque user reference     */
   ruc_buf_shaping_ctx_t     shaping_ctx;  /**< traffic shaping context    */
   uint32_t            retry_counter:8;  /**<buffer retry counter    */
   uint32_t            usrLen:24;  /**< user length occupied in the payload  */
   uint32_t            bufCount;   /**< BUF_ELEM-> max payload len           */
   uint8_t             inuse;        /**< that counter permit to prevent a buffer release */
   uint8_t             state;
   uint8_t             type;
   uint8_t           usrDestInfo[RUC_BUFFER_USER_ARRAY_SIZE];
   uint8_t           usrSrcInfo[RUC_BUFFER_USER_ARRAY_SIZE];
   ruc_pf_buf_t      callBackFct;   /**< called upon buffer release      */
  // 64BITS uint32_t            callBackParam;
  void            * callBackParam;
} ruc_buf_t;


#endif


