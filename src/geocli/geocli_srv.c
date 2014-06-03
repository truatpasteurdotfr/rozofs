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
 
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <errno.h>
#include <inttypes.h>
#include <rozofs/common/log.h>
#include <rozofs/core/ruc_common.h>
#include <rozofs/core/ruc_sockCtl_api.h>
#include "geocli_srv.h"
#include "rozofsmount.h"
#include <rozofs/core/ruc_timer_api.h>
#include <rozofs/core/uma_dbg_api.h>
#include <rozofs/core/rozofs_tx_api.h>
#include <rozofs/rpc/geo_replica_proto.h>
#include <rozofs/rozofs_timer_conf.h>
#include <rozofs/common/geo_replica_str.h>
#include "rzcp_file_ctx.h"
int geocli_period;  
int geocli_slow_polling_count;
int geocli_fast_polling_count;
int geocli_current_count;
int geocli_current_delay;

geocli_ctx_t *geocli_ctx_p= NULL;
int geo_replica_log_enable = 0;


static void geo_cli_periodic_ticker(void * param);



/*
**_________________________________________________________________________
*      PUBLIC FUNCTIONS
**_________________________________________________________________________
*/

/*
**_________________________________________________________________________
*/
/**
*  display a synchro entry file information
*/
char *geocli_display_one_file_info(char *p,geo_fid_entry_t *info_p)
{
  uint8_t * pFid;
  int idx;
  uint8_t   rozofs_safe = rozofs_get_rozofs_safe(info_p->layout);
  pFid = (uint8_t *) info_p->fid;  
  p+=sprintf(p,"   %d   |",info_p->layout);  
  /*
  ** display the fid
  */
  p += sprintf(p,"%2.2x%2.2x%2.2x%2.2x-%2.2x%2.2x-%2.2x%2.2x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x  |", 
               pFid[0],pFid[1],pFid[2],pFid[3],pFid[4],pFid[5],pFid[6],pFid[7],
	       pFid[8],pFid[9],pFid[10],pFid[11],pFid[12],pFid[13],pFid[14],pFid[15]);

  /*
  ** offset start and end
  */
  p +=sprintf(p,"%10"PRIu64" |",info_p->off_start);
  p +=sprintf(p,"%10"PRIu64" |",info_p->off_end);
  
  /*
  ** File only
  */
  p+=sprintf(p,"%3d |",info_p->cid);
  
  p += sprintf(p, "%3.3d", info_p->sids[0]);  
  for (idx = 1; idx < rozofs_safe; idx++) {
    p += sprintf(p,"-%3.3d", info_p->sids[idx]);
  } 
  p += sprintf(p,"\n");
  return p;
}


void show_geo_current_files(char * argv[], uint32_t tcpRef, void *bufRef) {
  char *pChar = uma_dbg_get_buffer();
  geo_fid_entry_t *info_p;
  int i;
     if (geocli_ctx_p == NULL)
     {
       sprintf(pChar,"No synchronization context available\n");
       uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer()); 
       return;  	  
     } 
     info_p = (geo_fid_entry_t*)geocli_ctx_p->data_record;
     pChar+=sprintf(pChar,"layout |           fid                        | off start |  off end  | cid|  file distribution (sids)\n");
     pChar+=sprintf(pChar,"-------+--------------------------------------+-----------+-----------+----+--------------------------\n");
     for (i = 0; i < geocli_ctx_p->nb_records;i++,info_p++)
     {
      pChar = geocli_display_one_file_info(pChar,info_p);     
     } 
     uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   	  
}


#define DISPLAY_UINT32_CONFIG(field)   pChar += sprintf(pChar,"%-25s = %u\n",#field, geocli_ctx_p->field); 
#define DISPLAY_STRING_CONFIG(field) \
  if (geocli_ctx_p->field == NULL) pChar += sprintf(pChar,"%-25s = NULL\n",#field);\
  else                    pChar += sprintf(pChar,"%-25s = %s\n",#field,geocli_ctx_p->field); 
  

void show_synchro_ctx(char * argv[], uint32_t tcpRef, void *bufRef) {
  char *pChar = uma_dbg_get_buffer();
  
  if (geocli_ctx_p == NULL)
  {
    sprintf(pChar,"No synchronization context available\n");
    goto out;
  }
  pChar+=sprintf(pChar,"%-25s = %u ms\n","Fast Polling",geocli_fast_polling_count*GEO_DEF_PERIO_MS);
  pChar+=sprintf(pChar,"%-25s = %u ms\n","Slow Polling",geocli_slow_polling_count*GEO_DEF_PERIO_MS);
  pChar+=sprintf(pChar,"%-25s = %u ms\n","Curr Polling",geocli_current_count*GEO_DEF_PERIO_MS);
  pChar+=sprintf(pChar,"%-25s = %u ms\n","Curr delay",geocli_current_delay*GEO_DEF_PERIO_MS);
  DISPLAY_UINT32_CONFIG(eid);
  DISPLAY_UINT32_CONFIG(site_id);
  DISPLAY_UINT32_CONFIG(first_record);  
  DISPLAY_UINT32_CONFIG(nb_records);
  DISPLAY_UINT32_CONFIG(cur_record);
out:
  uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
}  



#define SHOW_GEO_CLI_PROFILER_PROBE(probe) if (prof->probe[P_COUNT]) \
                    pChar += sprintf(pChar," %-24s | %15"PRIu64" | %15"PRIu64" | %15"PRIu64" | %9"PRIu64" | %18"PRIu64" |                 |\n",\
                    #probe,\
                    prof->probe[GEO_IDX_COUNT],\
                    prof->probe[GEO_IDX_TMO],\
                    prof->probe[GEO_IDX_ERR],\
                    prof->probe[GEO_IDX_COUNT]?prof->probe[GEO_IDX_TIME]/prof->probe[GEO_IDX_COUNT]:0,\
                    prof->probe[GEO_IDX_TIME]);


#define SHOW_RZCP_PROFILER_PROBE(probe) if (rzcp_profiler.probe[P_COUNT]) \
                    pChar += sprintf(pChar," %-24s | %15"PRIu64" | %15"PRIu64" | %15"PRIu64" | %9"PRIu64" | %18"PRIu64" | %15"PRIu64" |\n",\
                    #probe,\
                    rzcp_profiler.probe[RZCP_IDX_COUNT],\
                    rzcp_profiler.probe[RZCP_IDX_TMO],\
                    rzcp_profiler.probe[RZCP_IDX_ERR],\
                    rzcp_profiler.probe[RZCP_IDX_COUNT]?rzcp_profiler.probe[RZCP_IDX_TIME]/rzcp_profiler.probe[RZCP_IDX_COUNT]:0,\
                    rzcp_profiler.probe[RZCP_IDX_TIME],\
                    rzcp_profiler.probe[RZCP_IDX_BYTE_COUNT]);

char * show_geo_profiler_display(char * pChar) {
    geo_cli_profiler_t * prof= &geocli_ctx_p->profiler;   
           

    // Compute uptime for storaged process
    pChar +=  sprintf(pChar, "_______________________ EID = %d _______________________ \n",geocli_ctx_p->eid);
    pChar += sprintf(pChar, "   procedure              |     count       |     tmo         |     err         |  time(us) | cumulated time(us) | byte count      |\n");
    pChar += sprintf(pChar, "--------------------------+-----------------+-----------------+-----------------+-----------+--------------------+-----------------+\n");
    SHOW_GEO_CLI_PROFILER_PROBE(geo_sync_req);
    SHOW_GEO_CLI_PROFILER_PROBE(geo_sync_get_next_req);
    SHOW_GEO_CLI_PROFILER_PROBE(geo_sync_delete_req);
    SHOW_GEO_CLI_PROFILER_PROBE(geo_sync_close_req);
    SHOW_RZCP_PROFILER_PROBE(copy_file);
    SHOW_RZCP_PROFILER_PROBE(remove_file);

    return pChar;
}


static char * show_geo_profiler_help(char * pChar) {
  pChar += sprintf(pChar,"usage:\n");
  pChar += sprintf(pChar,"geo_profiler reset  : reset statistics\n");
  pChar += sprintf(pChar,"geo_profiler        : display statistics\n");  
  pChar += sprintf(pChar,"geo_profiler log    : enable error logging in syslog\n");
  pChar += sprintf(pChar,"geo_profiler no_log : disable error logging in syslog\n");
  return pChar; 
}
void show_geo_profiler(char * argv[], uint32_t tcpRef, void *bufRef) {
    char *pChar = uma_dbg_get_buffer();

     if (geocli_ctx_p == NULL)
     {
       sprintf(pChar,"No synchronization context available\n");
       uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer()); 
       return;  	  
     }
    if (argv[1] == NULL) {
      pChar +=sprintf(pChar,"syslog is %s for geo-replication errors\n",(geo_replica_log_enable==1)?"enabled":"disabled");
      pChar = show_geo_profiler_display(pChar);
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   	  
      return;
    }

    if (strcmp(argv[1],"reset")==0) {

	memset(&geocli_ctx_p->profiler,0,sizeof(geo_cli_profiler_t));
        memset(&rzcp_profiler,0,sizeof(rzcp_profiler_t));
	uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   
	return;	 
    }
    if (strcmp(argv[1],"log")==0) {
      geo_replica_log_enable = 1;
      rzcp_log_enable = 1;
      uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   	  
      return;
    }

    if (strcmp(argv[1],"no_log")==0) {
      geo_replica_log_enable = 0;
      rzcp_log_enable = 0;
      uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   	  
      return;
    }
      	     
    show_geo_profiler_help(pChar);
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   	  
    return;
}
/*
**____________________________________________________
*/
/**
*  start the periodic timer associated with the geo-replication

   @param period : frequency in ms
   
   @retval 0 on success
   @retval -1 on error (see errno for details)
*/

int geo_cli_start_timer(int period)
{

  struct timer_cell * geo_cli_periodic_timer;

  geo_cli_periodic_timer = ruc_timer_alloc(0,0);
  if (geo_cli_periodic_timer == NULL) {
    severe("cannot allocate timer cell");
    errno = EPROTO;
    return -1;
  }
  ruc_periodic_timer_start (geo_cli_periodic_timer, 
                            period,
 	                    geo_cli_periodic_ticker,
 			    NULL);
  return 0;

}
/*
**____________________________________________________
*/
/**
*  Call back function call upon a success rpc, timeout or any other rpc failure
*
 @param this : pointer to the transaction context
 @param param: pointer to the associated synchronisation context
 
 @return none
 */

void geo_cli_sync_req_cbk(void *this,void *param) 
{
   geo_sync_req_ret_t ret ;
   struct rpc_msg  rpc_reply;
   int errcode = 0;
   geocli_ctx_t *p;   
   int status;
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   xdrproc_t decode_proc = (xdrproc_t)xdr_geo_sync_req_ret_t;
   errno = 0;
    
   p = (geocli_ctx_t*)param;
    
   rpc_reply.acpted_rply.ar_results.proc = NULL;
    /*
    ** get the pointer to the transaction context:
    ** it is required to get the information related to the receive buffer
    */
    rozofs_tx_ctx_t  *rozofs_tx_ctx_p = (rozofs_tx_ctx_t*)this;     
    /*    
    ** get the status of the transaction -> 0 OK, -1 error (need to get errno for source cause
    */
    status = rozofs_tx_get_status(this);
    if (status < 0)
    {
       /*
       ** something wrong happened
       */
       errno = rozofs_tx_get_errno(this); 
       errcode = errno; 
       goto error; 
    }
    /*
    ** get the pointer to the receive buffer payload
    */
    recv_buf = rozofs_tx_get_recvBuf(this);
    if (recv_buf == NULL)
    {
       /*
       ** something wrong happened
       */
       errno = EFAULT;  
       errcode = errno; 
       goto error;         
    }
    payload  = (uint8_t*) ruc_buf_getPayload(recv_buf);
    payload += sizeof(uint32_t); /* skip length*/
    /*
    ** OK now decode the received message
    */
    bufsize = rozofs_tx_get_small_buffer_size();
    xdrmem_create(&xdrs,(char*)payload,bufsize,XDR_DECODE);
    /*
    ** decode the rpc part
    */
    if (rozofs_xdr_replymsg(&xdrs,&rpc_reply) != TRUE)
    {
     TX_STATS(ROZOFS_TX_DECODING_ERROR);
     errno = EPROTO;
     errcode = errno; 
     goto error;
    }
    /*
    ** ok now call the procedure to decode the message
    */
    memset(&ret,0, sizeof(ret));                    
    if (decode_proc(&xdrs,&ret) == FALSE)
    {
       TX_STATS(ROZOFS_TX_DECODING_ERROR);
       errno = EPROTO;
       xdr_free((xdrproc_t) decode_proc, (char *) &ret);
       errcode = errno; 
       goto error;
    }   
    
    if (ret.status == GEO_FAILURE) {
        errno = ret.geo_sync_req_ret_t_u.error;
        xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
        errcode = errno; 
        goto error;
    }
    /*
    ** save the received messsage in the synchro context
    */
    geo_sync_data_ret_t *data_p= &ret.geo_sync_req_ret_t_u.data;
    p->file_idx     = data_p->file_idx;
    p->remote_ref   = data_p->local_ref;
    p->first_record = data_p->first_record;
    p->last         = data_p->last;
    p->nb_records   = data_p->nb_records;
    p->cur_record   = 0;
    /*
    **  copy the received data in the synchro context
    */
    if (data_p->data.data_len > p->data_record_len)
    {
      errno = ERANGE;
      xdr_free((xdrproc_t) decode_proc, (char *) &ret);  
      errcode = errno; 
      goto error;  
    }
    memcpy(p->data_record,data_p->data.data_val,data_p->data.data_len);
    p->state_sync = GEOSYNC_ST_SYNC_FILE;    
    geocli_current_count = geocli_fast_polling_count;
    goto out;
error:
    if (errno == EAGAIN) geocli_current_count = geocli_slow_polling_count;
    p->state_sync =GEOSYNC_ST_IDLE;    
out:
    p->state = GEOCLI_ST_IDLE;
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);    
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf); 
      
    STOP_GEO_CLI_PROFILING(geo_sync_req,errcode);    
    return;
}

/*
**____________________________________________________
*/
/**
*  service associated with GEO_SYNC_REQ

  @param p :pointer to the client synchro context
 
 @retval none
*/
void geo_cli_geo_sync_req_processing(geocli_ctx_t *p)
{
    geo_sync_req_arg_t arg;
    int ret;
    int errcode = 0;
    
    p->delete_forbidden = 0;
    p->status_bitmap = 0;
    p->status_bitmap = ~p->status_bitmap;
    /*
    ** poll the exportd to figure out if there is some file
    ** to synchronize
    */
    START_GEO_CLI_PROFILING(geo_sync_req);
    
    arg.eid = p->eid;
    arg.site_id = p->site_id;
    arg.local_ref = p->local_ref;
    /*
    ** send the request towards the exportd
    */
    ret = rozofs_export_send_common(&exportclt,ROZOFS_TMR_GET(TMR_EXPORT_PROGRAM),GEO_PROGRAM, GEO_VERSION,
                          GEO_SYNC_REQ,(xdrproc_t) xdr_geo_sync_req_arg_t,(void *)&arg,
                          geo_cli_sync_req_cbk,p);
    
    if (ret < 0) 
    {
      errcode = errno;
      goto error;
    }    
    /*
    ** no error just waiting for the answer
    */
    geocli_ctx_p->state = GEOCLI_ST_INPRG;
    return;
error:
    /*
    ** release the buffer if has been allocated
    */
    STOP_GEO_CLI_PROFILING(geo_sync_req,errcode);
    geocli_ctx_p->state = GEOCLI_ST_IDLE;    
    p->state_sync       = GEOSYNC_ST_IDLE;    
    return;
}
/*
**____________________________________________________
*/
/**
*  Call back function associated with GEO_SYNC_GET_NEXT_REQ
*
 @param this : pointer to the transaction context
 @param param: pointer to the associated synchronisation context
 
 @return none
 */

void geo_cli_geo_sync_get_next_req_cbk(void *this,void *param) 
{
   geo_sync_req_ret_t ret ;
   int errcode = 0;
   struct rpc_msg  rpc_reply;
   geocli_ctx_t *p;   
   int status;
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   xdrproc_t decode_proc = (xdrproc_t)xdr_geo_sync_req_ret_t;
   errno = 0;
    
   p = (geocli_ctx_t*)param;
    
   rpc_reply.acpted_rply.ar_results.proc = NULL;
    /*
    ** get the pointer to the transaction context:
    ** it is required to get the information related to the receive buffer
    */
    rozofs_tx_ctx_t  *rozofs_tx_ctx_p = (rozofs_tx_ctx_t*)this;     
    /*    
    ** get the status of the transaction -> 0 OK, -1 error (need to get errno for source cause
    */
    status = rozofs_tx_get_status(this);
    if (status < 0)
    {
       /*
       ** something wrong happened
       */ 
       errno = rozofs_tx_get_errno(this);  
       errcode = errno;
       goto error; 
    }
    /*
    ** get the pointer to the receive buffer payload
    */
    recv_buf = rozofs_tx_get_recvBuf(this);
    if (recv_buf == NULL)
    {
       /*
       ** something wrong happened
       */
       errno = EFAULT;  
       errcode = errno;
       goto error;         
    }
    payload  = (uint8_t*) ruc_buf_getPayload(recv_buf);
    payload += sizeof(uint32_t); /* skip length*/
    /*
    ** OK now decode the received message
    */
    bufsize = rozofs_tx_get_small_buffer_size();
    xdrmem_create(&xdrs,(char*)payload,bufsize,XDR_DECODE);
    /*
    ** decode the rpc part
    */
    if (rozofs_xdr_replymsg(&xdrs,&rpc_reply) != TRUE)
    {
     TX_STATS(ROZOFS_TX_DECODING_ERROR);
     errno = EPROTO;
     errcode = errno;
     goto error;
    }
    /*
    ** ok now call the procedure to decode the message
    */
    memset(&ret,0, sizeof(ret));                    
    if (decode_proc(&xdrs,&ret) == FALSE)
    {
       TX_STATS(ROZOFS_TX_DECODING_ERROR);
       errno = EPROTO;
       xdr_free((xdrproc_t) decode_proc, (char *) &ret);
       errcode = errno;
       goto error;
    }   
    
    if (ret.status == GEO_FAILURE) {
        errno = ret.geo_sync_req_ret_t_u.error;
        xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
        errcode = errno;
        goto error;
    }
    /*
    ** save the received messsage in the synchro context
    */
    geo_sync_data_ret_t *data_p= &ret.geo_sync_req_ret_t_u.data;
    p->file_idx     = data_p->file_idx;
    p->remote_ref   = data_p->local_ref;
    p->first_record = data_p->first_record;
    p->last         = data_p->last;
    p->nb_records   = data_p->nb_records;
    p->cur_record   = 0;
    p->status_bitmap = 0;
    p->status_bitmap = ~p->status_bitmap;
    /*
    **  copy the received data in the synchro context
    */
    if (data_p->data.data_len > p->data_record_len)
    {
      errno = ERANGE;
      xdr_free((xdrproc_t) decode_proc, (char *) &ret);  
      errcode = errno;
      goto error;  
    }
    memcpy(p->data_record,data_p->data.data_val,data_p->data.data_len);
    p->state_sync = GEOSYNC_ST_SYNC_FILE;    
    goto out;
error:
    p->state_sync = GEOSYNC_ST_IDLE;    
out:
    p->state = GEOCLI_ST_IDLE;
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);    
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);   

    STOP_GEO_CLI_PROFILING(geo_sync_get_next_req,errcode);    
    return;
}

/*
**____________________________________________________
*/
/**
*  service associated with GEO_SYNC_GET_NEXT_REQ

  @param p :pointer to the client synchro context
 
 @retval none
*/
void geo_cli_geo_sync_get_next_req_processing(geocli_ctx_t *p)
{
    geo_sync_get_next_req_arg_t arg;
    int ret;
    int errcode = 0;
    /*
    ** poll the exportd to figure out if there is some file
    ** to synchronize
    */
    START_GEO_CLI_PROFILING(geo_sync_get_next_req);    

    arg.eid = p->eid;
    arg.site_id = p->site_id;
    arg.local_ref = p->local_ref;
    arg.remote_ref = p->remote_ref;
    arg.file_idx = p->file_idx;
    arg.next_record = p->first_record+p->nb_records;
    arg.status_bitmap = p->status_bitmap;
    /*
    ** send the request towards the exportd
    */
    ret = rozofs_export_send_common(&exportclt,ROZOFS_TMR_GET(TMR_EXPORT_PROGRAM),GEO_PROGRAM, GEO_VERSION,
                          GEO_SYNC_GET_NEXT_REQ,(xdrproc_t) xdr_geo_sync_get_next_req_arg_t,(void *)&arg,
                          geo_cli_geo_sync_get_next_req_cbk,p);
    
    if (ret < 0) 
    {
      errcode = errno;
      goto error;
    }
    /*
    ** no error just waiting for the answer
    */
    p->state = GEOCLI_ST_INPRG;
    return;
error:
    /*
    ** release the buffer if has been allocated
    */
    p->state      = GEOCLI_ST_IDLE;
    p->state_sync = GEOSYNC_ST_IDLE;   
    STOP_GEO_CLI_PROFILING(geo_sync_get_next_req,errcode);     
    return;
}

/*
**____________________________________________________
*/
/**
*  Call back function associated with GEO_SYNC_DELETE_REQ
*
 @param this : pointer to the transaction context
 @param param: pointer to the associated synchronisation context
 
 @return none
 */

void geo_cli_geo_sync_delete_req_cbk(void *this,void *param) 
{
   geo_status_ret_t ret ;
   int errcode = 0;
   struct rpc_msg  rpc_reply;
   geocli_ctx_t *p;   
   int status;
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   xdrproc_t decode_proc = (xdrproc_t)xdr_geo_status_ret_t;
   errno = 0;
    
   p = (geocli_ctx_t*)param;
    
   rpc_reply.acpted_rply.ar_results.proc = NULL;
    /*
    ** get the pointer to the transaction context:
    ** it is required to get the information related to the receive buffer
    */
    rozofs_tx_ctx_t  *rozofs_tx_ctx_p = (rozofs_tx_ctx_t*)this;     
    /*    
    ** get the status of the transaction -> 0 OK, -1 error (need to get errno for source cause
    */
    status = rozofs_tx_get_status(this);
    if (status < 0)
    {
       /*
       ** something wrong happened
       */
       errno = rozofs_tx_get_errno(this);  
       errcode = errno;
       goto error; 
    }
    /*
    ** get the pointer to the receive buffer payload
    */
    recv_buf = rozofs_tx_get_recvBuf(this);
    if (recv_buf == NULL)
    {
       /*
       ** something wrong happened
       */
       errno = EFAULT;  
       errcode = errno;
       goto error;         
    }
    payload  = (uint8_t*) ruc_buf_getPayload(recv_buf);
    payload += sizeof(uint32_t); /* skip length*/
    /*
    ** OK now decode the received message
    */
    bufsize = rozofs_tx_get_small_buffer_size();
    xdrmem_create(&xdrs,(char*)payload,bufsize,XDR_DECODE);
    /*
    ** decode the rpc part
    */
    if (rozofs_xdr_replymsg(&xdrs,&rpc_reply) != TRUE)
    {
     TX_STATS(ROZOFS_TX_DECODING_ERROR);
     errno = EPROTO;
     errcode = errno;
     goto error;
    }
    /*
    ** ok now call the procedure to decode the message
    */
    memset(&ret,0, sizeof(ret));                    
    if (decode_proc(&xdrs,&ret) == FALSE)
    {
       TX_STATS(ROZOFS_TX_DECODING_ERROR);
       errno = EPROTO;
       xdr_free((xdrproc_t) decode_proc, (char *) &ret);
       errcode = errno;
       goto error;
    }   
    
    if (ret.status == GEO_FAILURE) {
        errno = ret.geo_status_ret_t_u.error;
        xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
        errcode = errno;
        goto error;
    }
    p->state_sync = GEOSYNC_ST_IDLE;    
    goto out;
error:
    p->state_sync = GEOSYNC_ST_IDLE;    
out:
    p->state = GEOCLI_ST_IDLE;
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);    
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);   
    STOP_GEO_CLI_PROFILING(geo_sync_delete_req,errcode);    
    
    return;
}

/*
**____________________________________________________
*/
/**
*  Call back function associated with GEO_SYNC_CLOSE_REQ
*
 @param this : pointer to the transaction context
 @param param: pointer to the associated synchronisation context
 
 @return none
 */

void geo_cli_geo_sync_close_req_cbk(void *this,void *param) 
{
   geo_status_ret_t ret ;
   int errcode = 0;
   struct rpc_msg  rpc_reply;
   geocli_ctx_t *p;   
   int status;
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   xdrproc_t decode_proc = (xdrproc_t)xdr_geo_status_ret_t;
   errno = 0;
    
   p = (geocli_ctx_t*)param;
    
   rpc_reply.acpted_rply.ar_results.proc = NULL;
    /*
    ** get the pointer to the transaction context:
    ** it is required to get the information related to the receive buffer
    */
    rozofs_tx_ctx_t  *rozofs_tx_ctx_p = (rozofs_tx_ctx_t*)this;     
    /*    
    ** get the status of the transaction -> 0 OK, -1 error (need to get errno for source cause
    */
    status = rozofs_tx_get_status(this);
    if (status < 0)
    {
       /*
       ** something wrong happened
       */
       errno = rozofs_tx_get_errno(this);  
       errcode = errno;
       goto error; 
    }
    /*
    ** get the pointer to the receive buffer payload
    */
    recv_buf = rozofs_tx_get_recvBuf(this);
    if (recv_buf == NULL)
    {
       /*
       ** something wrong happened
       */
       errno = EFAULT;  
       errcode = errno;
       goto error;         
    }
    payload  = (uint8_t*) ruc_buf_getPayload(recv_buf);
    payload += sizeof(uint32_t); /* skip length*/
    /*
    ** OK now decode the received message
    */
    bufsize = rozofs_tx_get_small_buffer_size();
    xdrmem_create(&xdrs,(char*)payload,bufsize,XDR_DECODE);
    /*
    ** decode the rpc part
    */
    if (rozofs_xdr_replymsg(&xdrs,&rpc_reply) != TRUE)
    {
     TX_STATS(ROZOFS_TX_DECODING_ERROR);
     errno = EPROTO;
     errcode = errno;
     goto error;
    }
    /*
    ** ok now call the procedure to decode the message
    */
    memset(&ret,0, sizeof(ret));                    
    if (decode_proc(&xdrs,&ret) == FALSE)
    {
       TX_STATS(ROZOFS_TX_DECODING_ERROR);
       errno = EPROTO;
       xdr_free((xdrproc_t) decode_proc, (char *) &ret);
       errcode = errno;
       goto error;
    }   
    
    if (ret.status == GEO_FAILURE) {
        errno = ret.geo_status_ret_t_u.error;
        xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
        errcode = errno;
        goto error;
    }
    p->state_sync = GEOSYNC_ST_IDLE;    
    goto out;
error:
    p->state_sync = GEOSYNC_ST_IDLE;    
out:
    p->state = GEOCLI_ST_IDLE;
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);    
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);   
    STOP_GEO_CLI_PROFILING(geo_sync_close_req,errcode);    
    
    return;
}
/*
**____________________________________________________
*/
/**
*  service associated with GEO_SYNC_DELETE_REQ

  @param p :pointer to the client synchro context
 
 @retval none
*/
void geo_cli_geo_sync_close_req_processing(geocli_ctx_t *p)
{
    geo_sync_close_req_arg_t arg;
    int ret;
    int errcode = 0;
    /*
    ** poll the exportd to figure out if there is some file
    ** to synchronize
    */
    START_GEO_CLI_PROFILING(geo_sync_close_req);    

    /*
    ** check if delete is acceptable
    */
    arg.eid = p->eid;
    arg.site_id = p->site_id;
    arg.local_ref = p->local_ref;
    arg.remote_ref = p->remote_ref;
    arg.file_idx = p->file_idx;
    arg.status_bitmap = p->status_bitmap;
    /*
    ** send the request towards the exportd
    */
    ret = rozofs_export_send_common(&exportclt,ROZOFS_TMR_GET(TMR_EXPORT_PROGRAM),GEO_PROGRAM, GEO_VERSION,
                          GEO_SYNC_CLOSE_REQ,(xdrproc_t) xdr_geo_sync_close_req_arg_t,(void *)&arg,
                          geo_cli_geo_sync_close_req_cbk,p);
    
    if (ret < 0) 
    {
      errcode = errno;
      goto error;
    }
    /*
    ** no error just waiting for the answer
    */
    p->state = GEOCLI_ST_INPRG;
    return;
error:
    /*
    ** release the buffer if has been allocated
    */
    p->state      = GEOCLI_ST_IDLE;
    p->state_sync = GEOSYNC_ST_IDLE;    
    STOP_GEO_CLI_PROFILING(geo_sync_close_req,errcode);    
    return;
}

/*
**____________________________________________________
*/
/**
*  service associated with GEO_SYNC_DELETE_REQ

  @param p :pointer to the client synchro context
 
 @retval none
*/
void geo_cli_geo_sync_delete_req_processing(geocli_ctx_t *p)
{
    geo_sync_delete_req_arg_t arg;
    int ret;
    int errcode = 0;

    /*
    ** check if delete is acceptable
    */
    if (p->delete_forbidden != 0)
    {
      return geo_cli_geo_sync_close_req_processing(p);
    }
    /*
    ** poll the exportd to figure out if there is some file
    ** to synchronize
    */
    START_GEO_CLI_PROFILING(geo_sync_delete_req);    


    arg.eid = p->eid;
    arg.site_id = p->site_id;
    arg.local_ref = p->local_ref;
    arg.remote_ref = p->remote_ref;
    arg.file_idx = p->file_idx;
    /*
    ** send the request towards the exportd
    */
    ret = rozofs_export_send_common(&exportclt,ROZOFS_TMR_GET(TMR_EXPORT_PROGRAM),GEO_PROGRAM, GEO_VERSION,
                          GEO_SYNC_DELETE_REQ,(xdrproc_t) xdr_geo_sync_delete_req_arg_t,(void *)&arg,
                          geo_cli_geo_sync_delete_req_cbk,p);
    
    if (ret < 0) 
    {
      errcode = errno;
      goto error;
    }
    /*
    ** no error just waiting for the answer
    */
    p->state = GEOCLI_ST_INPRG;
    return;
error:
    /*
    ** release the buffer if has been allocated
    */
    p->state      = GEOCLI_ST_IDLE;
    p->state_sync = GEOSYNC_ST_IDLE;    
    STOP_GEO_CLI_PROFILING(geo_sync_delete_req,errcode);    
    return;
}
#if 0
/*
**____________________________________________________
*/
/**
*  the client has just receive the response to a sync_req

   - start the file synchro process
   
*/
void geo_cli_geo_file_sync_processing(geocli_ctx_t *p)
{
  
    if (p->last ) p->state_sync = GEOSYNC_ST_GETDEL;    
    else p->state_sync = GEOSYNC_ST_GETNEXT;
}
#endif
/*
**____________________________________________________
*/
/**
*  periodic processing associated to the idle state

  @param p : client synchro context
  @retval none
*/
void geo_cli_state_idle_processing(geocli_ctx_t *p)
{
   switch (p->state_sync)
   {
     case GEOSYNC_ST_IDLE:
      geo_cli_geo_sync_req_processing(p);
      break;
     case GEOSYNC_ST_SYNC_FILE:
      p->state = GEOCLI_ST_INPRG;           
      geo_cli_geo_file_sync_processing(p);
      break;
     case GEOSYNC_ST_GETNEXT:
      geo_cli_geo_sync_get_next_req_processing(p);
      break;
     case GEOSYNC_ST_GETDEL:
      geo_cli_geo_sync_delete_req_processing(p);
      break;
     default:
       p->state_sync = GEOSYNC_ST_IDLE;
      break;
   }
}
/*
**____________________________________________________
*/
/*
  Periodic timer expiration
*/
static void geo_cli_periodic_ticker(void * param) {

  geocli_current_delay--;
  if (geocli_current_delay > 0) return;
  geocli_current_delay = geocli_current_count;
  
  if (geocli_ctx_p == NULL) return;
  switch (geocli_ctx_p->state)
  {
    case GEOCLI_ST_IDLE:
      geo_cli_state_idle_processing(geocli_ctx_p);
      break;
    case GEOCLI_ST_INPRG:
      return;
    default:
      return;
  }
   
}

/*
**____________________________________________________
*/
/**
*  allocation of the geo-replication context

  @param none
  
  @retval 
*/
int geocli_alloc_ctx(uint16_t eid,uint16_t site)
{

   geocli_ctx_p = malloc(sizeof(geocli_ctx_t));
   if (geocli_ctx_p == NULL) return -1;
   
   memset(geocli_ctx_p,0,sizeof(geocli_ctx_t));
   geocli_ctx_p->state = GEOCLI_ST_IDLE;
   geocli_ctx_p->state_sync = GEOCLI_ST_IDLE;
   geocli_ctx_p->eid = eid;
   geocli_ctx_p->site_id = site;
   /*
   ** allocate a buffer for receiving the data
   */
   geocli_ctx_p->data_record = malloc(sizeof(geo_fid_entry_t)*GEO_MAX_RECORDS);
   if (geocli_ctx_p->data_record == NULL) return -1;
   geocli_ctx_p->data_record_len = sizeof(geo_fid_entry_t)*GEO_MAX_RECORDS;
   return 0;
   

}

/*
**____________________________________________________
*/
/**init of the client part of the geo-replication

  @param period: geo replication period in ms
  
  @retval 0 on success
  @retval -1 on error
*/
int geocli_init(int period,uint16_t eid,uint16_t site_id)
{
  geocli_period = period;
  int source_site;
  
  source_site = (site_id+1)&(ROZOFS_GEOREP_MAX_SITE-1);
  int ret;
  
  geocli_slow_polling_count = GEO_SLOW_POLL_COUNT;
  geocli_fast_polling_count = GEO_FAST_POLL_COUNT;
  geocli_current_count = geocli_slow_polling_count;
  geocli_current_delay = geocli_current_count;
  
  ret = geo_cli_start_timer(geocli_period);
  if (ret < 0) return ret;
  
  ret = geocli_alloc_ctx(eid,source_site);
  uma_dbg_addTopic("geo_ctx", show_synchro_ctx);
  uma_dbg_addTopic("geo_profiler", show_geo_profiler);
  uma_dbg_addTopic("geo_files", show_geo_current_files);

  return ret;
}
