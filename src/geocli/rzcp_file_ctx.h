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

#ifndef RZCP_FILE_H
#define RZCP_FILE_H

#include <rozofs/rozofs.h>
#include <rozofs/rpc/eclient.h>
#include <rozofs/rpc/sclient.h>
#include <rozofs/core/ruc_list.h>


typedef struct rzcp_profiler_t {
  uint64_t copy_file[6];
  uint64_t remove_file[6];
} rzcp_profiler_t;

#define RZCP_IDX_COUNT 0
#define RZCP_IDX_TMO 1
#define RZCP_IDX_ERR 2
#define RZCP_IDX_TIME 3
#define RZCP_IDX_SAVE_TIME 4
#define RZCP_IDX_BYTE_COUNT 5

#define START_RZCPY_PROFILING(the_probe)\
{\
    struct timeval tv;\
    rzcp_profiler.the_probe[RZCP_IDX_COUNT]++;\
    gettimeofday(&tv,(struct timezone *)0);\
    rzcp_profiler.the_probe[RZCP_IDX_SAVE_TIME] = MICROLONG(tv);\
}

#define RZCPY_PROFILING_BYTES(the_probe,bytes)\
{\
    rzcp_profiler.the_probe[RZCP_IDX_BYTE_COUNT] += bytes;\
}

#define STOP_RZCPY_PROFILING(the_probe,status)\
{\
    uint64_t toc;\
    struct timeval tv;\
    gettimeofday(&tv,(struct timezone *)0);\
    toc = MICROLONG(tv);\
	  switch (status) \
	  { \
	    case 0: \
	         break;\
	    case EAGAIN:\
	    rzcp_profiler.the_probe[RZCP_IDX_TMO]++;\
	    break;\
	    default:\
            if (rzcp_log_enable) severe("%s:%s",#the_probe,strerror(errno));\
	    rzcp_profiler.the_probe[RZCP_IDX_ERR]++;\
	    break;\
	  }\
          rzcp_profiler.the_probe[RZCP_IDX_TIME] += (toc - rzcp_profiler.the_probe[RZCP_IDX_SAVE_TIME]);\
    }


#define RZCP_MAX_CTX 16 /**< max number of context handled by the module */
#define RZCPY_MAX_BUF_LEN (1024*128)
typedef struct _rzcp_file_ctx
{
    fid_t fid;                      /**< file identifier                                */
    cid_t cid;                      /**< cluster id 0 for non regular files             */
    int   layout;                   /**< layout of the file                             */
    sid_t sids[ROZOFS_SAFE_MAX];    /**< sid of storage nodes target (regular file only)*/    
    int rotation_counter; /**< Rotation counter on file distribution. Incremented on each rotation */
    int rotation_idx;     /**< Rotation index within the rozo forward distribution */ 
    uint64_t off_start;       /**< starting offset        */
    int64_t initial_len;     /**< initial length         */
    uint64_t off_cur;         /**< current read offset    */
    uint64_t len_cur;         /**< current length         */
    int      retry_cur;  
    int      errcode;     
} rzcp_file_ctx_t;  
  
/**
* call back for end of read or write
  @param p: pointer to the context
  @param int : status of the operation
*/
typedef void (*rzcp_cpy_pf_t)(void*,int);

typedef struct _rzcp_copy_ctx
{
   ruc_obj_desc_t    link;        /**< To be able to chain the context on any list      */
   uint32_t            index;     /**< Index of the context                             */
   uint32_t            free;      /**< Is the context free or allocated TRUE/FALSE      */
   uint32_t            integrity; /**< the value of this field is incremented at  each ctx allocation */
   /*
   ** begin of the specific part
   */
   rzcp_file_ctx_t read_ctx;
   rzcp_file_ctx_t write_ctx;  
   int len2read;                 /**< effective length to read */
   int received_len;             /**< received length          */
   /*
   ** common to read/write 
   */
   int   storcli_idx;      /**< index of the strocli to use     */
   int   shared_buf_idx[SHAREMEM_PER_FSMOUNT];   /**< index of the shared buffer      */
   void *shared_buf_ref[SHAREMEM_PER_FSMOUNT];   /**< pointer to the shared buffer    */
   void *buffer;           /**< reference of the  buffer allocated thanks malloc */
   int   errcode;          /**< operation errcode               */
   uint64_t timestamp;     /**< current time for statistics     */
   rzcp_cpy_pf_t rzcp_copy_cbk;  /**< callback for end of read or write transaction*/
   /*
   ** caller information
   */
   void *opaque;
   rzcp_cpy_pf_t rzcp_caller_cbk;  /**< callback for end of copy service */     
} rzcp_copy_ctx_t;


/**
* transaction statistics
*/
typedef enum 
{
  RZCP_CTX_TIMEOUT=0,
  RZCP_CTX_NO_CTX_ERROR,
  RZCP_CTX_CTX_MISMATCH,
  RZCP_CTX_CPY_INIT_ERR,
  RZCP_CTX_CPY_BAD_READ_LEN,
  RZCP_CTX_CPY_READ_ERR,
  RZCP_CTX_CPY_WRITE_ERR,
  RZCP_CTX_CPY_ABORT_ERR,
  RZCP_CTX_COUNTER_MAX,
} rzcp_ctx_stats_e;

extern uint64_t rzcp_stats[];
extern int rzcp_log_enable;
extern rzcp_profiler_t rzcp_profiler;
#define RZCP_CTX_STATS(counter) \
{ rzcp_stats[counter]++;\
  if (rzcp_log_enable) severe("%s :%s",#counter,strerror(errno));\
}
/*
**____________________________________________________
*/
/**
   rzcp_module_init

  create the rzcopy context pool

@param     : context_count : number of contexts
@retval   : 0 : done
@retval     -1 : out of memory
 */
int rzcp_module_init(uint32_t context_count) ;
/*
**____________________________________________________
*/
/**
   rzcp_alloc

   create a  context
    That function tries to allocate a free  context. 
    In case of success, it returns the pointer to the context.
 
    @param     : none

    @retval   : <>NULL: pointer to the allocated context
    @retval    NULL if out of context.
*/
rzcp_copy_ctx_t *rzcp_alloc();
/*
**____________________________________________________
*/
/**
   delete a  context

   If the  context is out of limit, and  error is returned.

   @param     : pointer to the context
   @retval   : RUC_OK : context has been deleted
   @retval     RUC_NOK : out of limit index.

*/
uint32_t rzcp_free_from_ptr(rzcp_copy_ctx_t *p);
/*
**____________________________________________________
*/
/**
   delete a  context from idx
   

   @param     : index of the context
   
   @retval   : RUC_OK : context has been deleted
   @retval     RUC_NOK : out of limit index.
*/
uint32_t rzcp_free_from_idx(uint32_t context_id);

#endif
