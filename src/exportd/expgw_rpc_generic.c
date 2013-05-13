/**
* common context for export request received from rozofsmount
*/
typedef struct _expgw_rpc_generic_tx_t
{
  ruc_obj_desc_t link;
  uint32_t            index;         /**< Index of the MS */
  uint32_t            free;          /**< Is the context free or allocated TRUE/FALSE */
  uint32_t            integrity;     /**< the value of this field is incremented at  each MS ctx allocation  */
  sys_recv_pf_t    response_cbk;  /**< callback function associated with the response of the root transaction */
  xdrproc_t        xdr_result;
  int       errno;                /**< status of the operation (valid on callback only) */
  void      *user_ref;            /**< object index                    */
  void     *decoded_arg;          /**< pointer to the decoded argument */
  
} expgw_rpc_generic_tx_t;


/**
* structure with transaction module for rpc in non-blocking mode (generic API
*/
typedef struct _rozofs_rpc_ctx_t
{
  ruc_obj_desc_t link;
  uint32_t            index;         /**< Index of the MS */
  uint32_t            free;          /**< Is the context free or allocated TRUE/FALSE */
  uint32_t            integrity;     /**< the value of this field is incremented at  each MS ctx allocation  */
  
  sys_recv_pf_t    response_cbk;     /**< callback function associated with the response of the root transaction */
  xdrproc_t        xdr_result;       /**< rpc decoding procedure                                                 */
  void             *user_ref;        /**< object index                                                           */
  int              ret_len;          /**< length of the structure for decoding the returned parameters           */
  void            *ret_p;            /**< pointer to the structure used for storing the decoded response         */
  uint64_t *profiler_probe;          /**< pointer to the profiler counter */
  uint64_t profiler_time;            /**< profiler timestamp */
} rozofs_rpc_ctx_t;



/*
**______________________________________________________________________________
*/
/**
* ROZOFS Generic RPC Request transaction in non-blocking mode

 That service initiates RPC call towards the destination referenced by its associated load balancing group
 WHen the transaction is started, the application will received the response thanks the provided callback
 
 The first parameter is a user dependent reference and the second pointer is the pointer to the decoded
 area.
 In case of decoding error, transmission error, the second pointer is NULL and errno is asserted with the
 error.
 
 The array provided for decoding the response might be a static variable within  the user context or
 can be an allocated array. If that array has be allocated by the application it is up to the application
 to release it

 @param lbg_id     : reference of the load balancing group of the exportd
 @param prog       : program
 @param vers       : program version
 @param opcode     : metadata opcode
 @param encode_fct : encoding function
 @param msg2encode_p     : pointer to the message to encode
 @param decode_fct  : xdr function for message decoding
 @param ret: pointer to the array that is used for message decoding
 @parem ret_len : length of the array used for decoding
 @param recv_cbk   : receive callback function (for interpretation of the rpc result
 @param ctx_p      : pointer to the user context
 
 @retval 0 on success;
 @retval -1 on error,, errno contains the cause
 */
int rozofs_rpc_non_blocking_req_send (int lbg_id,uint32_t prog,uint32_t vers,
                                      int opcode,xdrproc_t encode_fct,void *msg2encode_p,
                                       xdrproc_t decode_fct,
                                       sys_recv_pf_t recv_cbk,void *ctx_p,
                                       void *ret,int ret_len) 
{
    DEBUG_FUNCTION;
   
    uint8_t           *arg_p;
    uint32_t          *header_size_p;
    rozofs_tx_ctx_t   *rozofs_tx_ctx_p = NULL;
    void              *xmit_buf = NULL;
    int               bufsize;
    int               ret;
    int               position;
    XDR               xdrs;    
	struct rpc_msg   call_msg;
    uint32_t         null_val = 0;

    rozofs_tx_rpc_ctx_t *rpc_ctx_p = NULL;

    /*
    ** allocate a rpc context
    */
    rozofs_rpc_p = rozofs_rpc_req_alloc();  
    if (rozofs_rpc_p == NULL) 
    {
       /*
       ** out of context
       */
       errno = ENOMEM;
       goto error;
    }    
    /*
    ** save the rpc parameter of the caller
    */
    rpc_ctx_p = &rozofs_tx_ctx_p->rpc_ctx;
    rpc_ctx_p->user_ref   = ctx_p;       /* save the user reference of the caller   */    
    rpc_ctx_p->xdr_result = decode_fct;  /* save the decoding procedure  */   
    rpc_ctx_p->response_cbk = recv_cbk ;
    rpc_ctx_p->ret_len  = ret_len;
    rpc_ctx_p->ret_p  = ret;
    START_RPC_REQ_PROFILING_START(rpc_ctx_p);
    /*
    ** allocate a transaction context
    */
    rozofs_tx_ctx_p = rozofs_tx_alloc();  
    if (rozofs_tx_ctx_p == NULL) 
    {
       /*
       ** out of context
       ** --> put a pending list for the future to avoid repluing ENOMEM
       */
       TX_STATS(ROZOFS_TX_NO_CTX_ERROR);
       errno = ENOMEM;
       goto error;
    }    

    /*
    ** allocate an xmit buffer
    */  
    xmit_buf = ruc_buf_getBuffer(ROZOFS_TX_LARGE_TX_POOL);
    if (xmit_buf == NULL)
    {
      /*
      ** something rotten here, we exit we an error
      ** without activating the FSM
      */
      TX_STATS(ROZOFS_TX_NO_BUFFER_ERROR);
      errno = ENOMEM;
      goto error;
    } 
    /*
    ** store the reference of the xmit buffer in the transaction context: might be useful
    ** in case we want to remove it from a transmit list of the underlying network stacks
    */
    rozofs_tx_save_xmitBuf(rozofs_tx_ctx_p,xmit_buf);
    /*
    ** get the pointer to the payload of the buffer
    */
    header_size_p  = (uint32_t*) ruc_buf_getPayload(xmit_buf);
    arg_p = (uint8_t*)(header_size_p+1);  
    /*
    ** create the xdr_mem structure for encoding the message
    */
    bufsize = ruc_buf_getMaxPayloadLen(xmit_buf);
    xdrmem_create(&xdrs,(char*)arg_p,bufsize,XDR_ENCODE);
    /*
    ** fill in the rpc header
    */
    call_msg.rm_direction = CALL;
    /*
    ** allocate a xid for the transaction 
    */
	call_msg.rm_xid             = rozofs_tx_alloc_xid(rozofs_tx_ctx_p); 
	call_msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	/* XXX: prog and vers have been long historically :-( */
	call_msg.rm_call.cb_prog = (uint32_t)prog;
	call_msg.rm_call.cb_vers = (uint32_t)vers;
	if (! xdr_callhdr(&xdrs, &call_msg))
    {
       /*
       ** THIS MUST NOT HAPPEN
       */
       TX_STATS(ROZOFS_TX_ENCODING_ERROR);
       errno = EPROTO;
       goto error;	
    }
    /*
    ** insert the procedure number, NULL credential and verifier
    */
    XDR_PUTINT32(&xdrs, (int32_t *)&opcode);
    XDR_PUTINT32(&xdrs, (int32_t *)&null_val);
    XDR_PUTINT32(&xdrs, (int32_t *)&null_val);
    XDR_PUTINT32(&xdrs, (int32_t *)&null_val);
    XDR_PUTINT32(&xdrs, (int32_t *)&null_val);
        
    /*
    ** ok now call the procedure to encode the message
    */
    if ((*encode_fct)(&xdrs,msg2encode_p) == FALSE)
    {
       TX_STATS(ROZOFS_TX_ENCODING_ERROR);
       errno = EPROTO;
       goto error;
    }
    /*
    ** Now get the current length and fill the header of the message
    */
    position = XDR_GETPOS(&xdrs);
    /*
    ** update the length of the message : must be in network order
    */
    *header_size_p = htonl(0x80000000 | position);
    /*
    ** set the payload length in the xmit buffer
    */
    int total_len = sizeof(*header_size_p)+ position;
    ruc_buf_setPayloadLen(xmit_buf,total_len);
    /*
    ** store the receive call back and its associated parameter
    */
    rozofs_tx_ctx_p->recv_cbk   = rozofs_rpc_generic_reply_cbk;
    rozofs_tx_ctx_p->user_param = rpc_ctx_p;    
    /*
    ** now send the message
    */
    ret = north_lbg_send(lbg_id,xmit_buf);
    if (ret < 0)
    {
       TX_STATS(ROZOFS_TX_SEND_ERROR);
       errno = EFAULT;
      goto error;  
    }
    TX_STATS(ROZOFS_TX_SEND);

    /*
    ** OK, so now finish by starting the guard timer
    */
    rozofs_tx_start_timer(rozofs_tx_ctx_p, 25);
    return 0;  
    
  error:
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);
    if (rpc_ctx_p != NULL) rozofs_rpc_req_free(rpc_ctx_p);
    return -1;    
}



/*
**__________________________________________________________________
*/
/**
*  Call back function call upon a success rpc, timeout or any other rpc failure
*
 @param this : pointer to the transaction context
 @param param: pointer to the associated rpc context
 
 @return none
 */

void rozofs_rpc_generic_reply_cbk(void *this,void *param) 
{
   struct rpc_msg  rpc_reply;
   rozofs_rpc_ctx_t *rpc_ctx_p = (rozofs_rpc_ctx_t*) param;
   void *ret;  
   int   ret_len;
   int status;
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   xdrproc_t decode_proc;
   int xdr_free_done;;
   
   /*
   ** get the decoding function from the user rpc context
   */
   decode_proc    = rpc_ctx_p->xdr_result;
   xdr_free_done  = 0;
   /*
   ** get the memory area in which we must decode the info
   */
   ret = rpc_ctx_p->ret_p;
   rel_len = rpc_ctx_p->rel_len;
   if ((ret == NULL) || (rel_len== 0)
   {
     severe("bad argurment ret %p rel_len %d",ret,rel_len);
     errno = EINVAL;
     goto error;
   }
   /*
   ** clear the memory used for decoding the reply
   */
   memset(ret,0,rel_len);

   rpc_reply.acpted_rply.ar_results.proc = NULL;
    /*
    ** get the pointer to the transaction context:
    ** it is required to get the information related to the receive buffer
    */
    rozofs_tx_ctx_t      *rozofs_tx_ctx_p = (rozofs_tx_ctx_t*)this;     
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
       goto error;         
    }
    payload  = (uint8_t*) ruc_buf_getPayload(recv_buf);
    payload += sizeof(uint32_t); /* skip length*/
    /*
    ** OK now decode the received message
    */
    bufsize = (int) ruc_buf_getPayloadLen(recv_buf);
    xdrmem_create(&xdrs,(char*)payload,bufsize,XDR_DECODE);
    /*
    ** decode the rpc part
    */
    if (rozofs_xdr_replymsg(&xdrs,&rpc_reply) != TRUE)
    {
     TX_STATS(ROZOFS_TX_DECODING_ERROR);
     errno = EPROTO;
     goto error;
    }
    /*
    ** ok now call the procedure to encode the message
    */
    if (decode_proc(&xdrs,ret) == FALSE)
    {
       TX_STATS(ROZOFS_TX_DECODING_ERROR);
       errno = EPROTO;
       goto error;
    } 
    goto out;
    
error:
    /*
    ** release the received buffer if one was present
    */
    if (recv_buf != NULL)
    {
       ruc_buf_freeBuffer(recv_buf);
       recv_buf = NULL;
    }

out:
    /*
    ** release the transaction context and the gateway context
    */
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);    
    /*
    ** call the user callback for returned parameter interpretation: caution recv_buf might be NULL!!
    */
    if (recv_buf == NULL)
      (*req_ctx_p->response_cbk)(rpc_ctx_p->usr_ref,NULL); 
    else
      (*req_ctx_p->response_cbk)(rpc_ctx_p->usr_ref,ret);      
    /*
    ** do not forget to release data allocated for xdr decoding
    */
    if (xdr_free_done == 0) xdr_free((xdrproc_t) decode_proc, (char *) ret);
    
    if (recv_buf != NULL)   ruc_buf_freeBuffer(recv_buf);
    if (rpc_ctx_p != NULL) rozofs_rpc_req_free(rpc_ctx_p);
    return;
}


