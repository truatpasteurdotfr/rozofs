int export_unlink_multiple(export_t * e, fid_t parent, char *name, fid_t fid,mattr_t * pattrs,lv2_entry_t *plv2) {
    int status = -1;
    lv2_entry_t *lv2=NULL;
    fid_t child_fid;
    uint32_t child_type;
    uint16_t nlink = 0;
    int fdp = -1;
    int ret;
    rozofs_inode_t *fake_inode_p;
    rmfentry_disk_t trash_entry;
    int root_dirent_mask = 0;
    int unknown_fid = 0;
    int deleted_fid_count = 0;
    char filename[1024];
    char *basename;
    int filecount;
    char *filename_p;
    int k;    

    /*
    ** search for rozofs key in the filename
    */
    if (strncmp(name,".@rozofs-mf@",12) == 0)
    {
      ret = rozofs_parse_object_name((char*)(name+12),&basename,&filecount);
      if (ret < 0)
      {
	errno = ENOTSUP;
	goto error;
      }
    }
    /*
    ** load the root_idx bitmap of the old parent
    */
    export_dir_load_root_idx_bitmap(e,parent,plv2);
    /*
    ** set global variables associated with the export
    */
    fdp = export_open_parent_directory(e,parent);
    if (get_mdirentry(plv2->dirent_root_idx_p,fdp, parent, name, child_fid, &child_type,&root_dirent_mask) != 0)
        goto out;

    if (S_ISDIR(child_type)) {
        errno = EISDIR;
        goto out;
    }
    /*
    ** do a loop on the file count
    */
    for (k = 0; k < filecount ; k++)
    {
      sprintf(filename,"%s.%d",basename,k);
      filename_p = filename;
      // Delete the mdirentry if exist
      ret =del_mdirentry(plv2->dirent_root_idx_p,fdp, parent, name, child_fid, &child_type,root_dirent_mask);
      if (ret != 0)
      {
	  if (errno != ENOENT) goto out;
	  /*
	  ** check the next entry
	  */
	  continue;
      }
      // Get mattrs of child to delete
      if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, child_fid)))
      {
	    unknown_fid++;
	    /*
	    ** check the next entry
	    */
	    continue;
      }
      deleted_fid_count++;    

      // Compute hash value for this fid
      uint32_t hash = 0;
      uint8_t *c = 0;
      for (c = lv2->attributes.s.attrs.fid; c != lv2->attributes.s.attrs.fid + 16; c++)
	  hash = *c + (hash << 6) + (hash << 16) - hash;
      hash %= RM_MAX_BUCKETS;	    
      /*
      ** prepare the trash entry
      */
      trash_entry.size = lv2->attributes.s.attrs.size;
      memcpy(trash_entry.fid, lv2->attributes.s.attrs.fid, sizeof (fid_t));
      trash_entry.cid = lv2->attributes.s.attrs.cid;
      memcpy(trash_entry.initial_dist_set, lv2->attributes.s.attrs.sids,
              sizeof (sid_t) * ROZOFS_SAFE_MAX);
      memcpy(trash_entry.current_dist_set, lv2->attributes.s.attrs.sids,
              sizeof (sid_t) * ROZOFS_SAFE_MAX);
      fake_inode_p =  (rozofs_inode_t *)parent;   
      ret = exp_trash_entry_create(e->trk_tb_p,fake_inode_p->s.usr_id,&trash_entry); 
      if (ret < 0)
      {
	 /*
	 ** error while inserting entry in trash file
	 */
	 severe("error on trash insertion name %s error %s",name,strerror(errno)); 
      }
      /*
      ** delete the metadata associated with the file
      */
      ret = exp_delete_file(e,lv2);
      /*
      * In case of geo replication, insert a delete request from the 2 sites 
      */
      if (e->volume->georep) 
      {
	/*
	** update the geo replication: set start=end=0 to indicate a deletion 
	*/
	geo_rep_insert_fid(e->geo_replication_tb[0],
                	   lv2->attributes.s.attrs.fid,
			   0/*start*/,0/*end*/,
			   e->layout,
			   lv2->attributes.s.attrs.cid,
			   lv2->attributes.s.attrs.sids);
	/*
	** update the geo replication: set start=end=0 to indicate a deletion 
	*/
	geo_rep_insert_fid(e->geo_replication_tb[1],
                	   lv2->attributes.s.attrs.fid,
			   0/*start*/,0/*end*/,
			   e->layout,
			   lv2->attributes.s.attrs.cid,
			   lv2->attributes.s.attrs.sids);
      }	
      /*
      ** Preparation of the rmfentry
      */
      rmfentry_t *rmfe = xmalloc(sizeof (rmfentry_t));
      export_rm_bins_pending_count++;
      memcpy(rmfe->fid, trash_entry.fid, sizeof (fid_t));
      rmfe->cid = trash_entry.cid;
      memcpy(rmfe->initial_dist_set, trash_entry.initial_dist_set,
              sizeof (sid_t) * ROZOFS_SAFE_MAX);
      memcpy(rmfe->current_dist_set, trash_entry.current_dist_set,
              sizeof (sid_t) * ROZOFS_SAFE_MAX);
      memcpy(rmfe->trash_inode,trash_entry.trash_inode,sizeof(fid_t));
      list_init(&rmfe->list);
      /* Acquire lock on bucket trash list
      */
      if ((errno = pthread_rwlock_wrlock
              (&e->trash_buckets[hash].rm_lock)) != 0) {
	  severe("pthread_rwlock_wrlock failed: %s", strerror(errno));
	  // Best effort
      }
      /*
      ** Check size of file 
      */
      if (lv2->attributes.s.attrs.size >= RM_FILE_SIZE_TRESHOLD) {
	  // Add to front of list
	  list_push_front(&e->trash_buckets[hash].rmfiles, &rmfe->list);
      } else {
	  // Add to back of list
	  list_push_back(&e->trash_buckets[hash].rmfiles, &rmfe->list);
      }

      if ((errno = pthread_rwlock_unlock
              (&e->trash_buckets[hash].rm_lock)) != 0) {
	  severe("pthread_rwlock_unlock failed: %s", strerror(errno));
	  // Best effort
      }
      // Update the nb. of blocks
      if (export_update_blocks(e,
              -(((int64_t) lv2->attributes.s.attrs.size + ROZOFS_BSIZE_BYTES(e->bsize) - 1)
              / ROZOFS_BSIZE_BYTES(e->bsize))) != 0) {
	  severe("export_update_blocks failed: %s", strerror(errno));
	  // Best effort
      }
      // Remove from the cache (will be closed and freed)
      lv2_cache_del(e->lv2_cache, child_fid);
    }  
    /*
    ** al the subfile have been deleted so  Update export files
    */
    if (export_update_files(e, 0-deleted_fid_count) != 0)
      goto out;

    // Update parent
    plv2->attributes.s.attrs.mtime = plv2->attributes.s.attrs.ctime = time(NULL);
    plv2->attributes.s.attrs.children--;

    // Write attributes of parents
    if (export_lv2_write_attributes(e->trk_tb_p,plv2) != 0)
        goto out;
    /*
    ** return the parent attributes
    */
    memcpy(pattrs, &plv2->attributes.s.attrs, sizeof (mattr_t));
    status = 0;

out:
    /*
    ** check if parent root idx bitmap must be updated
    */
    if (plv2 != NULL) export_dir_flush_root_idx_bitmap(e,parent,plv2->dirent_root_idx_p);
    
    if(fdp != -1) close(fdp);
    return status;
}
