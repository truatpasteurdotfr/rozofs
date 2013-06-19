
uint32_t exportd_storage_host_count = 0;                          /**< number of host storage in the configuration of an eid  */
ep_storage_node_t exportd_storage_host_table[STORAGE_NODES_MAX];  /**< configuration for each storage */

epgw_conf_ret_t  export_storage_conf;                 /**< preallocated area to build storage configuration message */

/*
 *_______________________________________________________________________
 */
/**
*  Init of the array that is used for building an exportd configuration message
  That array is allocated during the initialization of the exportd and might be
  released upon the termination of the exportd process
  
  @param none
  
  @retval 0 on success
  @retval -1 on error
 */
int exportd_init_storage_configuration_message()
{
  ep_storage_node_t *storage_cnf_p;
  int i;
  /*
  ** clear the memory that contains the area for building a storage configuration message
  */
  memset(export_storage_conf,0,epgw_conf_ret_t);
  export_storage_conf.status_gw.ep_conf_ret_t_u.export.storage_nodes.storage_nodes_len = 0;
  export_storage_conf.status_gw.ep_conf_ret_t_u.export.storage_nodes.storage_nodes_val = exportd_storage_host_table;
  /*
  ** init of the storage node array
  */
  storage_cnf_p = &expgw_conf.status_gw.ep_conf_ret_t_u.export.storage_nodes_val;
  
  for (i = 0; i < STORAGE_NODES_MAX; i++,storage_cnf_p++)
  {
    storage_cnf_p->host = malloc( ROZOFS_HOSTNAME_MAX+1);
    if (storage_cnf_p->host == NULL)
    {
      severe("exportd_init_storage_configuration_message: out of memory");
      return -1;
    
    }
    storage_cnf_p->host[0] = 0;  
  }
}


/*
 *_______________________________________________________________________
 */
 /**
 *  That API is intended to be called by ep_conf_storage_1_svc() 
    prior to build the configuration message
    
    The goal is to clear the number of storages and to clear the
    number of sid per storage entry
    
    @param none
    retval none
*/
void exportd_reinit_storage_configuration_message()
{
  ep_storage_node_t *storage_cnf_p = exportd_storage_host_table;
  export_storage_conf.status_gw.ep_conf_ret_t_u.export.storage_nodes.storage_nodes_len = 0;

  for (i = 0; i < STORAGE_NODES_MAX; i++,storage_cnf_p++)
  {
    storage_cnf_p->sids_nb = 0;
  }
}
/*
**______________________________________________________________________________
*/
/**
*   exportd: Get the configuration of the storaged for a given eid

  : returns to the rozofsmount the list of the
*   volume,clusters and storages
*/
epgw_conf_ret_t *ep_conf_storage_1(void *resp) {
    static epgw_conf_ret_t ret;
    int total_len;
    
    memset(ret,0,sizeof(ret);
    

    XDR               xdrs; 
    int total_len = -1 ;
    int size = 1024*64;
    char *pchar = malloc(size);
    xdrmem_create(&xdrs,(char*)pchar,size,XDR_DECODE);    
  
    /* decode the message */
    if (xdr_epgw_mount_ret_t(&xdrs,&ret) == FALSE)
    {
      severe("encoding error"); 
      goto error   
    } 
    else
    {   
     total_len = xdr_getpos(&xdrs) ;
    }
    
    if (ret.status_gw.status == EP_FAILURE)
    {
      severe("bad message error");     
      goto error;      
    }

   int stor_len =  ret.status_gw.ep_conf_ret_t_u.export.storage_nodes.storage_nodes_len;
   ep_cnf_storage_node_t *stor_p;
   stor_p = ret.status_gw.ep_conf_ret_t_u.export.storage_nodes.storage_nodes_val;
   for (i = 0; i < stor_len ; i++,stor_p++)
   {
     int k;
     for (k= 0; k < stor_p->sids_nb; k++)
     {
       stor_p->sids[k],
       stor_p->cids[k],     
     }   
   }  

error:
    if (pchar != NULL) free(pchar);

}
