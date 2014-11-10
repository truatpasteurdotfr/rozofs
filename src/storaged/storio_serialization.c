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

#include "storio_serialization.h"


uint64_t   storage_unqueued_req[STORIO_DISK_THREAD_MAX_OPCODE]={0};
uint64_t   storage_queued_req[STORIO_DISK_THREAD_MAX_OPCODE]={0};
uint64_t   storage_direct_req[STORIO_DISK_THREAD_MAX_OPCODE]={0};


/*_______________________________________________________________________
* Display serialization counter debug help
*/
static char * display_serialization_counters_help(char * pChar) {
  pChar += sprintf(pChar,"usage:\n");
  pChar += sprintf(pChar,"serialization reset       : reset serialization counter\n");
  return pChar; 
}
/*_______________________________________________________________________
* Reset serialization counters
*/
static inline void reset_serialization_counters(void) {
  memset(storage_queued_req,0, sizeof(storage_queued_req));
  memset(storage_unqueued_req,0, sizeof(storage_unqueued_req));
  memset(storage_direct_req,0, sizeof(storage_direct_req));
}
/*_______________________________________________________________________
* Display opcode
*/
char * serialize_opcode_string(int opcode) {
  switch(opcode) {
    case STORIO_DISK_THREAD_READ: return "read";
    case STORIO_DISK_THREAD_WRITE: return "write";
    case STORIO_DISK_THREAD_TRUNCATE: return "truncate";
    case STORIO_DISK_THREAD_REMOVE: return "remove";
    case STORIO_DISK_THREAD_REMOVE_CHUNK: return "remove_chunk";
    default: return "Unknown";
  }
}
/*_______________________________________________________________________
* Serialization debug function
*/
void display_serialization_counters (char * argv[], uint32_t tcpRef, void *bufRef) {
  char          * p = uma_dbg_get_buffer();
  int             opcode;
  
  if (argv[1] != NULL) {
    if (strcmp(argv[1],"reset")==0) {
      reset_serialization_counters();
      uma_dbg_send(tcpRef,bufRef,TRUE,"Reset Done");
      return;
    }
    p = display_serialization_counters_help(p);
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
    return;    
  }
    
  p += sprintf(p, "+--------------+------------------+------------------+------------------+\n");
  p += sprintf(p, "| %12s | %16s | %16s | %16s |\n", "request","direct","queued","unqueued");
  p += sprintf(p, "+--------------+------------------+------------------+------------------+\n");
  for (opcode=1; opcode<STORIO_DISK_THREAD_MAX_OPCODE; opcode++) {  
    p += sprintf(p, "| %12s | %16llu | %16llu | %16llu |\n", 
                serialize_opcode_string(opcode),
                (long long unsigned int)storage_direct_req[opcode],
                (long long unsigned int)storage_queued_req[opcode],
		(long long unsigned int)storage_unqueued_req[opcode]);       
  }
  p += sprintf(p, "+--------------+------------------+------------------+------------------+\n");
  uma_dbg_send(tcpRef,bufRef,TRUE,uma_dbg_get_buffer());    
}
/*_______________________________________________________________________
* Initialize dserialization counters 
*/
void serialization_counters_init(void) {
  reset_serialization_counters();
  uma_dbg_addTopic_option("serialization", display_serialization_counters, UMA_DBG_OPTION_RESET); 
}
/*
**___________________________________________________________
** Put a request in the run queue
*/
static inline int storio_serialization_direct_run(storio_device_mapping_t * dev_map_p, rozorpc_srv_ctx_t *req_ctx_p) {
  list_remove(&req_ctx_p->list);
  list_push_back(&dev_map_p->running_request,&req_ctx_p->list);
  storage_direct_req[req_ctx_p->opcode]++;  
  return 1;  
}
/*
**___________________________________________________________
** Put a request in the run queue
*/
static inline int storio_serialization_unqueue_run(storio_device_mapping_t * dev_map_p, rozorpc_srv_ctx_t *req_ctx_p, uint64_t toc) {
  list_remove(&req_ctx_p->list);
  list_push_back(&dev_map_p->running_request,&req_ctx_p->list);
  storage_unqueued_req[req_ctx_p->opcode]++;    

  storio_disk_thread_intf_send(dev_map_p->device,req_ctx_p,toc) ;
  return 1;  
}
/*
**___________________________________________________________
** Put a request in the wait queue
*/
static inline int storio_serialization_wait(storio_device_mapping_t * dev_map_p, rozorpc_srv_ctx_t *req_ctx_p) {
  list_remove(&req_ctx_p->list);
  list_push_back(&dev_map_p->waiting_request,&req_ctx_p->list);
  storage_queued_req[req_ctx_p->opcode]++;
  return 0;
}
/*
**___________________________________________________________
** Get chunk information from a write request
*/
static inline int storio_check_write_allocate_chunk(storio_device_mapping_t * dev_map_p, rozorpc_srv_ctx_t *req_ctx_p) {
  sp_write_arg_t * write_arg_p = (sp_write_arg_t *) ruc_buf_getPayload(req_ctx_p->decoded_arg);
  int block_per_chunk          = ROZOFS_STORAGE_NB_BLOCK_PER_CHUNK(write_arg_p->bsize);
  int chunk                    = write_arg_p->bid/block_per_chunk;

  /* Is chunk number valid */
  if (chunk >= ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) return 0;

  /* Is this chunk already allocated */
  if ((dev_map_p->device[chunk] == ROZOFS_EOF_CHUNK) || (dev_map_p->device[chunk] == ROZOFS_EMPTY_CHUNK)) {
    return 1;
  }

  /* Does the request requires more than one chunk */    
  if (((write_arg_p->bid%block_per_chunk)+write_arg_p->nb_proj) <= block_per_chunk){
    return 0;
  }     

  /* Is next chunk valid */   
  chunk++; 
  if (chunk >= ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) return 0;

  /* Is the next chunk already allocated */
  if ((dev_map_p->device[chunk] == ROZOFS_EOF_CHUNK) || (dev_map_p->device[chunk] == ROZOFS_EMPTY_CHUNK)) {
    return 1;
  }
  return 0;
} 
/*
**___________________________________________________________
** Check whether request must run alone
*/
static inline int storio_is_request_exclusive(storio_device_mapping_t * dev_map_p, rozorpc_srv_ctx_t *req_ctx_p) {

  /*
  ** While file location is unknown. Let' serialize every request
  */
  if (dev_map_p->device[0] == ROZOFS_UNKNOWN_CHUNK) {
    return 1;
  }

  switch (req_ctx_p->opcode) {
  
    case STORIO_DISK_THREAD_READ:     return 0;
    case STORIO_DISK_THREAD_TRUNCATE: return 1;
    case STORIO_DISK_THREAD_REMOVE:   return 1;
    case STORIO_DISK_THREAD_REMOVE_CHUNK:   return 1;
    case STORIO_DISK_THREAD_WRITE:
    {
      sp_write_arg_t * write_arg_p = (sp_write_arg_t *) ruc_buf_getPayload(req_ctx_p->decoded_arg);
      int block_per_chunk          = ROZOFS_STORAGE_NB_BLOCK_PER_CHUNK(write_arg_p->bsize);
      int chunk                    = write_arg_p->bid/block_per_chunk;

      /* Is chunk number valid */
      if (chunk >= ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) return 0;

      /* Is this chunk already allocated */
      if ((dev_map_p->device[chunk] == ROZOFS_EOF_CHUNK) || (dev_map_p->device[chunk] == ROZOFS_EMPTY_CHUNK)) {
	return 1;
      }

      /* Does the request requires more than one chunk */    
      if (((write_arg_p->bid%block_per_chunk)+write_arg_p->nb_proj) <= block_per_chunk){
	return 0;
      }     

      /* Is next chunk valid */   
      chunk++; 
      if (chunk >= ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) return 0;

      /* Is the next chunk already allocated */
      if ((dev_map_p->device[chunk] == ROZOFS_EOF_CHUNK) || (dev_map_p->device[chunk] == ROZOFS_EMPTY_CHUNK)) {
	return 1;
      }
      return 0;      
    }
    default:
      return 1;
  }    
    

} 
/*
**___________________________________________________________
*/
int storio_serialization_begin(storio_device_mapping_t * dev_map_p, rozorpc_srv_ctx_t *req_ctx_p) {
  list_t            * p;
  rozorpc_srv_ctx_t * req;
   
  /*
  ** When waiting queue is not empty, put the request behind
  */
  if (!list_empty(&dev_map_p->waiting_request)) {
    return storio_serialization_wait(dev_map_p,req_ctx_p);
  } 

  /*
  ** Waiting queue is empty. If running queue too, please go
  */
  if (list_empty(&dev_map_p->running_request)) { 
    return storio_serialization_direct_run(dev_map_p,req_ctx_p);
  }  

  /*
  ** Waiting queue is empty and some requests are running
  */  
  
  
  /*
  ** The new request can not run with an other one
  */
  if (storio_is_request_exclusive(dev_map_p,req_ctx_p)) {
    return storio_serialization_wait(dev_map_p,req_ctx_p);  
  }

  /* 
  ** The new request can run with an other one. 
  ** Check the running requests can too.
  */
  list_for_each_forward(p, &dev_map_p->running_request) {
 
    req = list_entry(p, rozorpc_srv_ctx_t, list);

    if (storio_is_request_exclusive(dev_map_p,req)) {
      return storio_serialization_wait(dev_map_p,req_ctx_p);
    }
      
  }    
    
  return storio_serialization_direct_run(dev_map_p,req_ctx_p);      
}
/*
**___________________________________________________________
*/
int storio_serialization_begin_old(storio_device_mapping_t * dev_map_p, rozorpc_srv_ctx_t *req_ctx_p) {
  
  /*
  ** When waiting queue is not empty, put the request behind
  */
  if (!list_empty(&dev_map_p->waiting_request)) {
    return storio_serialization_wait(dev_map_p,req_ctx_p);
  } 

  /*
  ** Waiting queue is empty. If running queue too, please go
  */
  if (list_empty(&dev_map_p->running_request)) { 
    return storio_serialization_direct_run(dev_map_p,req_ctx_p);
  }  

  return storio_serialization_wait(dev_map_p,req_ctx_p);    
}
/*
**___________________________________________________________
*/
void storio_serialization_end(storio_device_mapping_t * dev_map_p, rozorpc_srv_ctx_t *req_ctx_p) {	
  uint64_t            toc;    
  struct timeval      tv;
  list_t            * p, * q;
  rozorpc_srv_ctx_t * req;
  
  
  /*
  ** Remove this request
  */
  list_remove(&req_ctx_p->list);
  
  if (list_empty(&dev_map_p->waiting_request)) {
  
    /*
    ** No waiting request to run
    */
    if (req_ctx_p->opcode == STORIO_DISK_THREAD_REMOVE) {
      storio_device_mapping_release_entry(dev_map_p);
    }
    
    return;
  }  
  
  /*
  ** Waiting list is not empty
  */


  /* 
  ** Check running requests are not exclusive 
  */
  p = NULL;
  list_for_each_forward(p, &dev_map_p->running_request) {

    req = list_entry(p, rozorpc_srv_ctx_t, list);

    /*
    ** Is this request exclusive
    */      
    if (storio_is_request_exclusive(dev_map_p,req)) {
      return;
    }
  }
    

  gettimeofday(&tv,(struct timezone *)0);
  toc = MICROLONG(tv);


  /* 
  ** Loop on waiting requests 
  */
  p = q = NULL;
  list_for_each_forward_safe(p, q, &dev_map_p->waiting_request) {

    req = list_entry(p, rozorpc_srv_ctx_t, list);

    if (list_empty(&dev_map_p->running_request)) {
      storio_serialization_unqueue_run(dev_map_p,req, toc);
      continue;
    }

    /*
    ** Check if this request is exclusive
    */
    if (storio_is_request_exclusive(dev_map_p,req)) {
      return;  
    }
    
    storio_serialization_unqueue_run(dev_map_p,req, toc);
  }
  
  return;    
}
/*
**___________________________________________________________
*/
void storio_serialization_end_old(storio_device_mapping_t * dev_map_p, rozorpc_srv_ctx_t *req_ctx_p) {	
  uint64_t            toc;    
  struct timeval      tv;
  list_t            * pw;
  rozorpc_srv_ctx_t * reqw;
  
  /*
  ** Remove this request
  */
  list_remove(&req_ctx_p->list);
  
  if (list_empty(&dev_map_p->waiting_request)) {
  
    /*
    ** No waiting request to run
    */
    if (req_ctx_p->opcode == STORIO_DISK_THREAD_REMOVE) {
      storio_device_mapping_release_entry(dev_map_p);
    }
    
    return;
  }  
  
  /*
  ** Waiting list is not empty
  */

  gettimeofday(&tv,(struct timezone *)0);
  toc = MICROLONG(tv);


  /* Loop on waiting requests */
  list_for_each_forward(pw, &dev_map_p->waiting_request) {

    reqw = list_entry(pw, rozorpc_srv_ctx_t, list);

    /*
    ** Running queue is empty, please go
    */
    if (list_empty(&dev_map_p->running_request)) { 
      storio_serialization_unqueue_run(dev_map_p,reqw,toc);
      continue;
    }  
    
    return;
  }
  
  return;    
}
