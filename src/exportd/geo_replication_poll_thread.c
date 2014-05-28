
/**
*  Polling thread that control the geo-replication disk flush
 */
static void *georep_poll_thread(void *v) {
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    list_t *iterator = NULL;
    rpcclt_t export_cnx;
    struct timeval timeout_mproto;
    void *ret;

    // Set the frequency of calls
    struct timespec ts = {5, 0};
    /*
    ** initiate a local connection towards the exportd: use localhost as
    ** destination host
    */
    timeout_mproto.tv_sec = 10;
    timeout_mproto.tv_usec = 0;
    /*
    ** init of the rpc context before attempting to connect with the 
    ** exportd
    */
    init_rpcctl_ctx(&export_cnx);

    while(1)
    {
    /*
    ** OK now initiate the connection with the exportd
    */
    if (rpcclt_initialize
            (&export_cnx, "127.0.0.1", EXPORT_PROGRAM, EXPORT_VERSION,
            ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE, 0,
            timeout_mproto) == 0) break;
     /*
     ** wait for a while and then re-attempt to re-connect
     */
     nanosleep(&ts, NULL);

    }
    for (;;) {
    
      ret = ep_geo_poll_1(NULL, &export_cnx);
      if (ret == NULL) {
          errno = EPROTO;
	  severe("geo-replication polling error);
      }

    nanosleep(&ts, NULL);
    }
    return 0;
}
