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



    status = expgw_rpc_req_generic( lbg_id,EXPORT_PROGRAM,EXPORT_VERSION, EP_GETATTR, 
                                                 (xdrproc_t)xdr_epgw_mfile_arg_t , &arg_attr, 
                                                  (xdrproc_t)xdr_epgw_mfile_arg_t ,
                                                 expgw_generic_export_reply_cbk,
                                                 req_ctx_p) ;



/**
* API for creation a transaction towards an exportd

 The reference of the north load balancing is extracted for the client structure
 fuse_ctx_p:
 That API needs the pointer to the current fuse context. That nformation will be
 saved in the transaction context as userParam. It is intended to be used later when
 the client gets the response from the server
 encoding function;
 For making that API generic, the caller is intended to provide the function that
 will encode the message in XDR format. The source message that is encoded is 
 supposed to be pointed by msg2encode_p.
 Since the service is non-blocking, the caller MUST provide the callback function 
 that will be used for decoding the message
 

 @param lbg_id     : reference of the load balancing group of the exportd
 @param prog       : program
 @param vers       : program version
 @param opcode     : metadata opcode
 @param encode_fct : encoding function
 @msg2encode_p     : pointer to the message to encode
 @param recv_cbk   : receive callback function
 @param ctx_p      : pointer to the user context
 
 @retval 0 on success;
 @retval -1 on error,, errno contains the cause
 */

int expgw_rpc_req_generic(int lbg_id,uint32_t prog,uint32_t vers,
                                       int opcode,xdrproc_t encode_fct,void *msg2encode_p,
                                       xdrproc_t decode_fct,
                                       sys_recv_pf_t recv_cbk,void *ctx_p) 
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

    expgw_rpc_generic_tx_t *rpc_ctx_p = NULL;
    /*
    ** allocate a buffer fro the rpc tx
    */
    rpc_ctx_p = expgw_rpc_gen_alloc_tx_ctx()
    if (rpc_ctx_p == NULL) 
    {
       /*
       ** out of context
       */
       errno = ENOMEM;
       goto error;
    } 
    rpc_ctx_p->user_ref   = ctx_p;       /* save the user reference of the caller   */    
    rpc_ctx_p->xdr_result = decode_fct;  /* save the decoding procedure  */   
    rpc_ctx_p->response_cbk = recv_cbk ;
    rpc_ctx_p->errno  = 0;
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
    rozofs_tx_ctx_p->recv_cbk   = expgw_generic_rpc_reply_cbk;
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
    if (rpc_ctx_p != NULL) expgw_rpc_gen_free_tx_ctx(rpc_ctx_p);
    return -1;    
}



/*
**__________________________________________________________________
*/
/**
*  Call back function call upon a success rpc, timeout or any other rpc failure
*
 @param this : pointer to the transaction context
 @param param: pointer to the associated gateway context
 
 @return none
 */

void expgw_generic_rpc_reply_cbk(void *this,void *param) 
{
   struct rpc_msg  rpc_reply;
   char argument[EXPGW_RPC_MAX_DECODE_BUFFER];
   expgw_ctx_t *req_ctx_p = (expgw_ctx_t*) param;
   void *ret;  
   int status;
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   xdrproc_t decode_proc = req_ctx_p->xdr_result;
   int xdr_free_done = 0;
   
   
   ret = argument;
   
   memset(ret,0,EXPGW_RPC_MAX_DECODE_BUFFER);

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
    req_ctx_p->decoded_arg = ret;
    if (recv_buf == NULL)
      (*req_ctx_p->response_cbk)(req_ctx_p->usr_ref,NULL); 
    else
      (*req_ctx_p->response_cbk)(req_ctx_p->usr_ref,ret);      
    /*
    ** do not forget to release data allocated for xdr decoding
    */
    if (xdr_free_done == 0) xdr_free((xdrproc_t) decode_proc, (char *) ret);
    if (recv_buf != NULL)   ruc_buf_freeBuffer(recv_buf);
    if (req_ctx_p != NULL)   expgw_rpc_gen_free_tx_ctx(req_ctx_p);

    return;
}


