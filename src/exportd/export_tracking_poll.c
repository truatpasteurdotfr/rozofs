
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

#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <attr/xattr.h>
#include <sys/vfs.h>
#include <uuid/uuid.h>
#include <sys/types.h>
#include <inttypes.h>
#include <dirent.h>
#include <time.h>

#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/list.h>
#include <rozofs/rozofs_srv.h>
#include <rozofs/common/profile.h>
#include <rozofs/common/export_track.h>
#include <rozofs/rpc/epproto.h>
#include <rozofs/rpc/mclient.h>

#include "config.h"
#include "export.h"
#include "cache.h"
#include "mdirent.h"
#include "xattr_main.h"
/*
**__________________________________________________________________
*/

/**
  attempt to either concatenate or delete a tracking file
  
  @param top_p : pointer to the main tracking file structure 
  @param slice_id : index of the slice for which the delete is attempted 
  @param file_id : index of the tracking file to concatenate or delete
  @param tracking_file_hdr_p: pointer to the tracking file header
  @param main_tracking_header: pointer to the main tracking file header
  @param type: type of the element (used to index statistics array)
  
*/
void exp_trck_delete_attempt( exp_trck_top_header_t *top_p,
                              uint8_t slice_id,uint64_t file_id,
			      exp_trck_file_header_t *tracking_file_hdr_p,
			      int nb_entries,
                              exp_trck_header_t *main_tracking_header,
			      int type)
{
  char pathname[1024];
  char newpathname[1024];
  int ret;
  int i;
  exp_trk_th_stats_t *stats_p = &exp_trk_th_stats_p[type];
  
  /*
  ** go through the file header and check if we can truncate the file
  */
  int nb_empty_entries = 0;
  i = nb_entries-1;
  while(i >=0)
  {
    if (tracking_file_hdr_p->inode_idx_table[i]!= 0xffff)
    {
      break;
    } 
    nb_empty_entries++;  
    i--;
  }
  if (nb_empty_entries == 0)
  {
    /*
    ** nothing more to do
    */
    return;
  }
  /*
  ** check if the has can be deleted
  */
  if (nb_empty_entries == nb_entries)
  {
    sprintf(pathname,"%s/%d/trk_%llu",top_p->root_path,slice_id,(long long unsigned int)file_id);
    ret = unlink(pathname);
    if (ret < 0)
    {
      severe("cannot delete %s:%s\n",pathname,strerror(errno));
    }
    /*
    ** update statistics
    */
    stats_p->counter[TRK_TH_INODE_DEL_STATS]+=1;
    /*
    ** check if the file tracking correspond to the first index of the main tracking file
    */
    if (main_tracking_header->first_idx == file_id)
    {
      /*
      ** update the main tracking file
      */
      main_tracking_header->first_idx++;
      exp_trck_write_main_tracking_file(top_p->root_path,slice_id,0,sizeof(uint64_t),&main_tracking_header->first_idx);
      return;
    }
    return;    
  }
  /*
  ** OK, the file is not empty, but we can truncate it
  */
  off_t len =  sizeof(exp_trck_file_header_t)+top_p->max_attributes_sz*(nb_entries-nb_empty_entries);
  sprintf(newpathname,"%s/%d/trk_%llu",top_p->root_path,slice_id,(long long unsigned int)file_id);
  stats_p->counter[TRK_TH_INODE_TRUNC_STATS]+=1;

  ret = truncate(newpathname,len);
  if (ret < 0)
  {
     severe("%s truncate failure: %s",newpathname,strerror(errno));  
  }  
}



/*
**_______________________________________________________________________________
*/
/**
* The purpose of that service is to check the content of the tracking file and
  to truncate or delete the tracking file when is possible for releasing blocks
  to the filesystem

   e: pointer to the export
   type : type of the tracking files
   
*/   
int exp_trck_inode_release_poll(export_t * e,int type)
{
    int slice = 0;
    uint64_t file_id;
    int ret;
    exp_trck_file_header_t tracking_buffer_src;
    exp_trck_top_header_t *top_p = e->trk_tb_p->tracking_table[type];
    exp_trck_header_memory_t *slice_hdr_p;
    int nb_entries;
    /*
    ** check the slice
    */
    for (slice = 0; slice < EXP_TRCK_MAX_USER_ID; slice++)
    {
      slice_hdr_p = top_p->entry_p[slice];
      for (file_id = slice_hdr_p->entry.first_idx; file_id < slice_hdr_p->entry.last_idx; file_id++)
      { 
         /*
	 ** read the header of the current tracking file: the service returns the number of entries in the
	 ** file
	 */
	 ret = exp_metadata_get_tracking_file_header(top_p,slice,file_id,&tracking_buffer_src,&nb_entries);
	 if (ret < 0)
	 {
	   if (errno != ENOENT)
	   {
              printf("error while reading metadata header %s\n",strerror(errno));
	     // exit(-1);
	     continue;
	   }
	   /*
	   ** update the main tracking file if the file_id matches the first_idx of the
	   ** main tracking file
	   */
	   if (slice_hdr_p->entry.first_idx == file_id)
	   {
	     /*
	     ** update the main tracking file
	     */
	     slice_hdr_p->entry.first_idx++;
	     exp_trck_write_main_tracking_file(top_p->root_path,slice,0,sizeof(uint64_t),&slice_hdr_p->entry.first_idx);
	   }	   
	   continue;
	 }
	 /*
	 ** attempt to delete/truncate or concatenate the tracking file
         */
         exp_trck_delete_attempt(top_p,slice,file_id,
	                         &tracking_buffer_src,nb_entries,&slice_hdr_p->entry,type);       
      }       
    }
    return 0;
}
