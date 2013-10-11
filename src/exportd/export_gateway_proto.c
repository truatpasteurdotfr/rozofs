
/*
**______________________________________________________________________________
*/
/**
 *  Message to provide a ROZOFS with the export gateway configuration: EP_CONF_EXPGW
*/
ep_gw_gateway_configuration_ret_t *ep_conf_expgw_1_svc(ep_path_t * arg, struct svc_req * req) {
    static ep_gw_gateway_configuration_ret_t ret;
    eid_t *eid = NULL;
    export_t *exp;
    int i = 0;
    list_t *iterator;
    list_t *iterator_expgw;    
    ep_gateway_configuration_t *expgw_conf_p= &ret.status_gw.ep_gateway_configuration_ret_t_u.config;

    START_PROFILING(ep_conf_gateway);
	ret.hdr.eid          = 0;      /* NS */
	ret.hdr.nb_gateways  = 0; /* NS */
	ret.hdr.gateway_rank = 0; /* NS */
	ret.hdr.hash_config  = 0; /* NS */

    // XXX exportd_lookup_id could return export_t *
    if (!(eid = exports_lookup_id(*arg)))
        goto error;
    if (!(exp = exports_lookup_export(*eid)))
        goto error;

    /* Get lock on config */
    if ((errno = pthread_rwlock_rdlock(&config_lock)) != 0) {
        goto error;
    }            
    expgw_conf_p->eid.eid_len = 0;
    expgw_conf_p->exportd_port = 0;
    expgw_conf_p->gateway_port = 0;

    list_for_each_forward(iterator, &exports) 
    {
       export_entry_t *entry = list_entry(iterator, export_entry_t, list);
       expgw_eid_table[expgw_conf_p->eid.eid_len] = entry->export.eid;
       expgw_conf_p->eid.eid_len++;
    }
    /*
    ** unlock exportd config
    */
    if ((errno = pthread_rwlock_unlock(&config_lock)) != 0) 
    {
        severe("can unlock config_lock, potential dead lock.");
        goto error;
    }
    if (expgw_conf_p->eid.eid_len == 0)
    {
      severe(" no eid in the exportd configuration !!");
      if ((errno = pthread_rwlock_unlock(&config_lock)) != 0) 
      {
          severe("can unlock config_lock, potential dead lock.");
      }
      error = EPROTO;
      goto error;
    }

    expgw_conf_p->hdr.export_id            = 0;
    expgw_conf_p->hdr.nb_gateways          = 0;
    expgw_conf_p->hdr.gateway_rank         = 0;
    expgw_conf_p->hdr.configuration_indice = export_configuration_file_hash;
    
    list_for_each_forward(iterator, &expgws) 
    {
        expgw_entry_t *entry = list_entry(iterator, expgw_entry_t, list);
        expgw_conf_p->hdr.export_id = entry->expgw.daemon_id;
        expgw_t *expgw = &entry->expgw;
        /*
        ** loop on the storage
        */
        
        list_for_each_forward(iterator_expgw, &expgw->expgw_storages) 
        {
          expgw_storage_t *entry = list_entry(iterator_expgw, expgw_storage_t, list);
          /*
          ** copy the hostname
          */
 
          strcpy((char*)expgw_host_table[expgw_conf_p->gateway_host.gateway_host_len].host, entry->host);
 
          info("building the configuration host %s",(char*)expgw_host_table[expgw_conf_p->gateway_host.gateway_host_len].host); 
          expgw_conf_p->gateway_host.gateway_host_len++;
          expgw_conf_p->hdr.nb_gateways++;
        }
    }
    if ((errno = pthread_rwlock_unlock(&config_lock)) != 0) 
    {
        severe("can't unlock expgws, potential dead lock.");
        goto error;
    } 
//    info("exportd id  %d",expgw_conf_p->hdr.export_id);          
//    info("nb_gateways %d",expgw_conf_p->hdr.nb_gateways);          
//    info("nb_eid      %d",expgw_conf_p->eid.eid_len);   
     

    ret.status_gw.status = EP_SUCCESS;

    goto out;
error:
    ret.status_gw.status = EP_FAILURE;
    ret.status_gw.ep_gateway_configuration_ret_t_u.error = errno;
out:

//    STOP_PROFILING(ep_conf_gateway);
    return &ret;
}
