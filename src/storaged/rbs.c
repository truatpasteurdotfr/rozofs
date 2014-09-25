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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h> 
#include <sys/wait.h>

#include <rozofs/common/list.h>
#include <rozofs/common/htable.h>
#include <rozofs/rozofs.h>
#include <rozofs/rozofs_srv.h>
#include <rozofs/rpc/rpcclt.h>
#include <rozofs/rpc/mclient.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/spproto.h>
#include <rozofs/rpc/eproto.h>
#include <rozofs/core/rozofs_host2ip.h>

#include "sconfig.h"
#include "storage.h"
#include "rbs_sclient.h"
#include "rbs_eclient.h"
#include "rbs.h"
#include "storage.h"

DECLARE_PROFILING(spp_profiler_t);


/* Print debug trace */
#define DEBUG_RBS 0
/* Time in seconds between two passes of reconstruction */
#define RBS_TIME_BETWEEN_2_PASSES 30

/* Local storage to rebuild */


static rozofs_rebuild_header_file_t st2rebuild;
static storage_t * storage_to_rebuild = &st2rebuild.storage;

/* List of cluster(s) */
static list_t cluster_entries;
/* RPC client for exports server */
static rpcclt_t rpcclt_export;
/* nb. of retries for get bins on storages */
static uint32_t retries = 10;

static inline int fid_cmp(void *key1, void *key2) {
    return memcmp(key1, key2, sizeof (fid_t));
}


static int ckeck_mtime(int fd, struct timespec st_mtim) {
    struct stat st;

    if (fstat(fd, &st) != 0)
        return -1;

    if (st_mtim.tv_sec == st.st_mtim.tv_sec &&
            st_mtim.tv_nsec == st.st_mtim.tv_nsec)
        return 0;

    return -1;
}


/*
** FID hash table to prevent registering 2 times
**   the same FID for rebuilding
*/
#define FID_TABLE_HASH_SIZE  (16*1024)

#define FID_MAX_ENTRY      31
typedef struct _rb_fid_entries_t {
    int                        count;
    int                        padding;
    struct _rb_fid_entries_t * next;   
    fid_t                      fid[FID_MAX_ENTRY];
} rb_fid_entries_t;

rb_fid_entries_t ** rb_fid_table=NULL;
uint64_t            rb_fid_table_count=0;


/*
**
*/
static inline unsigned int fid_hash(void *key) {
    uint32_t hash = 0;
    uint8_t *c;
    for (c = key; c != key + 16; c++)
        hash = *c + (hash << 6) + (hash << 16) - hash;
    return hash % FID_TABLE_HASH_SIZE;
}
static inline void rb_hash_table_initialize() {
  int size;
  
  size = sizeof(void *)*FID_TABLE_HASH_SIZE;
  rb_fid_table = malloc(size);
  memset(rb_fid_table,0,size);
  rb_fid_table_count = 0;
}

int rb_hash_table_search(fid_t fid) {
  int      i;
  unsigned int idx = fid_hash(fid);
  rb_fid_entries_t * p;
  fid_t            * pF;
  
  p = rb_fid_table[idx];
  
  while (p != NULL) {
    pF = &p->fid[0];
    for (i=0; i < p->count; i++,pF++) {
      if (memcmp(fid, pF, sizeof (fid_t)) == 0) return 1;
    }
    p = p->next;
  }
  return 0;
}
rb_fid_entries_t * rb_hash_table_new(idx) {
  rb_fid_entries_t * p;
    
  p = (rb_fid_entries_t*) malloc(sizeof(rb_fid_entries_t));
  p->count = 0;
  p->next = rb_fid_table[idx];
  rb_fid_table[idx] = p;
  
  return p;
}
rb_fid_entries_t * rb_hash_table_get(idx) {
  rb_fid_entries_t * p;
    
  p = rb_fid_table[idx];
  if (p == NULL)                 p = rb_hash_table_new(idx);
  if (p->count == FID_MAX_ENTRY) p = rb_hash_table_new(idx);  
  return p;
}
void rb_hash_table_insert(fid_t fid) {
  unsigned int idx = fid_hash(fid);
  rb_fid_entries_t * p;
  
  p = rb_hash_table_get(idx);
  memcpy(p->fid[p->count],fid,sizeof(fid_t));
  p->count++;
  rb_fid_table_count++;
}
void rb_hash_table_delete() {
  int idx;
  rb_fid_entries_t * p, * pNext;
  
  if (rb_fid_table == NULL) return;
  
  for (idx = 0; idx < FID_TABLE_HASH_SIZE; idx++) {
    
    p = rb_fid_table[idx];
    while (p != NULL) {
      pNext = p->next;
      free(p);
      p = pNext;
    }
  }
  
  free(rb_fid_table);
  rb_fid_table = NULL;
}





/** Get name of temporary rebuild directory
 *
 */
char rebuild_directory_name[FILENAME_MAX];
char * get_rebuild_directory_name() {
  pid_t pid = getpid();
  sprintf(rebuild_directory_name,"/tmp/rbs.%d",pid);  
  return rebuild_directory_name;
}
extern projection_t rbs_projections[ROZOFS_SAFE_MAX];
uint8_t prj_id_present[ROZOFS_SAFE_MAX];

int rbs_restore_one_spare_entry(storage_t * st, rb_entry_t * re, char * path, int device_id, uint8_t spare_idx) {
    int status = -1;
    int i = 0;
    int fd = -1;
    int ret = -1;
    struct stat loc_file_stat;
    uint32_t loc_file_init_blocks_nb = 0;
    uint8_t version = 0;
    bid_t first_block_idx = 0;
    uint64_t file_size = 0;
    rbs_storcli_ctx_t working_ctx;
    int block_idx = 0;
    uint8_t rbs_prj_idx_table[ROZOFS_SAFE_MAX];
    int     count;
    rbs_inverse_block_t * pBlock;
    int prj_count;
    uint16_t prj_ctx_idx;
    uint16_t projection_id;
    char   *  pforward = NULL;
    rozofs_stor_bins_hdr_t * rozofs_bins_hdr_p;
    rozofs_stor_bins_footer_t * rozofs_bins_foot_p;
    rozofs_stor_bins_hdr_t * bins_hdr_local_p;    
    bin_t * loc_read_bins_p = NULL;
    size_t local_len_read;
    int    remove_file;
    int    is_fid_faulty;

        
    // Get rozofs layout parameters
    uint8_t  layout            = re->layout;
    uint32_t bsize             = re->bsize;
    uint32_t bbytes            = ROZOFS_BSIZE_BYTES(bsize);
    uint8_t  rozofs_safe       = rozofs_get_rozofs_safe(layout);
    uint8_t  rozofs_forward    = rozofs_get_rozofs_forward(layout);
    uint8_t  rozofs_inverse    = rozofs_get_rozofs_inverse(layout);
    uint16_t disk_block_size   = (rozofs_get_max_psize(layout,bsize)*sizeof (bin_t)) 
                               + sizeof (rozofs_stor_bins_hdr_t) 
			       + sizeof(rozofs_stor_bins_footer_t);
    uint16_t disk_block_bins_size   = disk_block_size/sizeof(bin_t);
    uint32_t requested_blocks  = ROZOFS_BLOCKS_IN_BUFFER(re->bsize);
    uint32_t nb_blocks_read_distant = requested_blocks;

    // Clear the working context
    memset(&working_ctx, 0, sizeof (rbs_storcli_ctx_t));

    // Check that this file already exists on the storage to rebuild
    if (stat(path, &loc_file_stat) == 0) {
    
        // Compute the nb. of blocks	
        loc_file_init_blocks_nb = (loc_file_stat.st_size) / disk_block_size;
		
	// Allocate memory to read the local file	
        loc_read_bins_p = xmalloc(requested_blocks*disk_block_size);
	
	remove_file = 1; // This file probably needs to be removed	
    }
    else {
        remove_file = 0;
    }

    // While we can read in the bins file
    while (nb_blocks_read_distant == requested_blocks) {
         
        // Free bins read in previous round
	for (i = 0; i < rozofs_safe; i++) {
            if (working_ctx.prj_ctx[i].bins) {
        	free(working_ctx.prj_ctx[i].bins);
		working_ctx.prj_ctx[i].bins      = NULL;
	    }	   
	    working_ctx.prj_ctx[i].prj_state = PRJ_READ_IDLE;
	}
    
        // Read every available bins
	ret = rbs_read_all_available_proj(re->storages, spare_idx, layout, bsize, st->cid,
                                	  re->dist_set_current, re->fid, first_block_idx,
                                	  requested_blocks, &nb_blocks_read_distant,
                                	  &working_ctx);
					  
	// Reading at least inverse projection has failed				  
        if (ret != 0) {
            remove_file	= 0;// Better keep the file	
            errno = EIO;	
            goto out;
        }
	
	if (nb_blocks_read_distant == 0) break; // End of file	
	 
	// If local file exist and has some interesting blocks
	local_len_read = 0;
	if (loc_file_init_blocks_nb > first_block_idx) {
	
            // Read local bins
            ret = storage_read(st, &device_id, layout, bsize, re->dist_set_current, 1/*spare*/, re->fid,
                    first_block_idx, nb_blocks_read_distant, loc_read_bins_p,
                    &local_len_read, &file_size, &is_fid_faulty);

            if (ret != 0) {
	        local_len_read = 0;
                severe("storage_read failed: %s", strerror(errno));
            }
            else {
                // Compute the nb. of local blocks read
                local_len_read = local_len_read / disk_block_size;
	    }		      	
	}
	
	// Loop on the received blocks
        pBlock = &working_ctx.block_ctx_table[0];	
        for (block_idx = 0; block_idx < nb_blocks_read_distant; block_idx++,pBlock++) {

            count = rbs_count_timestamp_tb(working_ctx.prj_ctx, layout, bsize, block_idx,
                                           rbs_prj_idx_table, 
			  		   &pBlock->timestamp,
                                           &pBlock->effective_length);
					   
	    // Less than rozofs_inverse projection. Can not regenerate anything 
	    // from what has been read				   	
	    if (count < 0) {
                remove_file = 0;// Better keep the file	    
        	errno = EIO;	
        	goto out;	      
	    }
	    
	    // All projections have been read. Nothing to regenerate for this block
	    if (count >= rozofs_forward) continue;

	       
            // Enough projection read to regenerate data, but not as much as required.   


            // Is this block already written on local disk
            if (block_idx < local_len_read) {

                bins_hdr_local_p = (rozofs_stor_bins_hdr_t *) 
			(loc_read_bins_p + (disk_block_bins_size * block_idx));
		if (bins_hdr_local_p->s.timestamp == pBlock->timestamp) {
		    // Check footer too
		    projection_id = bins_hdr_local_p->s.projection_id;
		    rozofs_bins_foot_p = (rozofs_stor_bins_footer_t*)((bin_t*)(bins_hdr_local_p+1)+rozofs_get_psizes(layout,bsize,projection_id));	

        	    // Compare timestamp of local and distant block
        	    if (rozofs_bins_foot_p->timestamp == bins_hdr_local_p->s.timestamp) {
                       remove_file = 0;// This file must exist
                       continue; // Check next block
        	    }
	        }	 
	    }   
	    

            // Case of the empty block
            if (pBlock->timestamp == 0) {
	    
	       prj_ctx_idx = rbs_prj_idx_table[0];
	           
               // Store the projections on local bins file	
               ret = storage_write(st, &device_id, layout, bsize, re->dist_set_current, 1/*spare*/,
                		   re->fid, first_block_idx+block_idx, 1, version,
                		   &file_size, working_ctx.prj_ctx[prj_ctx_idx].bins,
				   &is_fid_faulty);
               remove_file = 0;	// This file must exist   
               if (ret <= 0) {
                   severe("storage_write failed %s: %s", path, strerror(errno));
                   goto out;
               }	       
	       continue;
	    }  		
	    
	    // Need to regenerate a projection and need 1rst to regenerate initial data.	
	    
	    // Allocate memory for initial data
            if (working_ctx.data_read_p == NULL) {
	      working_ctx.data_read_p = xmalloc(bbytes);
	    }		


            memset(prj_id_present,0,sizeof(prj_id_present));
	    
            for (prj_count = 0; prj_count < count; prj_count++) {

        	// Get the pointer to the beginning of the projection and extract
        	// the projection ID
        	prj_ctx_idx = rbs_prj_idx_table[prj_count];

        	rozofs_stor_bins_hdr_t *rozofs_bins_hdr_p =
                	(rozofs_stor_bins_hdr_t*) (working_ctx.prj_ctx[prj_ctx_idx].bins
                	+ (disk_block_bins_size * block_idx));

        	// Extract the projection_id from the header and fill the table
        	// of projections for the block block_idx for each projection
        	projection_id = rozofs_bins_hdr_p->s.projection_id;
		
		prj_id_present[projection_id] = 1;
		
		if (prj_count < rozofs_inverse) {
        	    rbs_projections[prj_count].angle.p = rozofs_get_angles_p(layout,projection_id);
        	    rbs_projections[prj_count].angle.q = rozofs_get_angles_q(layout,projection_id);
        	    rbs_projections[prj_count].size    = rozofs_get_psizes(layout,bsize,projection_id);
        	    rbs_projections[prj_count].bins    = (bin_t*) (rozofs_bins_hdr_p + 1);
		}   
            }

            // Inverse data for the block (first_block_idx + block_idx)
            transform_inverse((pxl_t *) working_ctx.data_read_p,
                	      rozofs_inverse,
                	      bbytes / rozofs_inverse / sizeof (pxl_t),
                	      rozofs_inverse, rbs_projections);
	    
	    // Find out which projection id to regenerate
            for (projection_id = 0; projection_id < rozofs_safe; projection_id++) {
	        if (prj_id_present[projection_id] == 0) break;
	    }
	    
	    // Allocate memory for regenerated projection
	    if (pforward == NULL) pforward = xmalloc(disk_block_size);
	    rozofs_bins_hdr_p = (rozofs_stor_bins_hdr_t *) pforward;
	    rozofs_bins_foot_p = (rozofs_stor_bins_footer_t*)((bin_t*)(rozofs_bins_hdr_p+1)+rozofs_get_psizes(layout,bsize,projection_id));	
	    
	    // Describe projection to rebuild 
            rbs_projections[projection_id].angle.p = rozofs_get_angles_p(layout,projection_id);
            rbs_projections[projection_id].angle.q = rozofs_get_angles_q(layout,projection_id);
            rbs_projections[projection_id].size    = rozofs_get_psizes(layout,bsize,projection_id);
            rbs_projections[projection_id].bins    = (bin_t*) (rozofs_bins_hdr_p + 1);

            // Generate projections to rebuild
            transform_forward_one_proj((const bin_t *)working_ctx.data_read_p, 
	                               rozofs_inverse, 
	                               bbytes / rozofs_inverse / sizeof (pxl_t),
				       projection_id, 
				       rbs_projections);
				
	    // Fill projection header			       
	    rozofs_bins_hdr_p->s.projection_id     = projection_id;			       
	    rozofs_bins_hdr_p->s.effective_length  = pBlock->effective_length;
	    rozofs_bins_hdr_p->s.timestamp         = pBlock->timestamp;			       
            rozofs_bins_hdr_p->s.version           = 0;
            rozofs_bins_hdr_p->s.filler            = 0;

            rozofs_bins_foot_p->timestamp          = pBlock->timestamp;	
	        
            // Store the projections on local bins file	
            ret = storage_write(st, &device_id, layout, bsize, re->dist_set_current, 1/*spare*/,
                		re->fid, first_block_idx+block_idx, 1, version,
                		&file_size, (const bin_t *)rozofs_bins_hdr_p,
				&is_fid_faulty);
            remove_file = 0;// This file must exist		   
            if (ret <= 0) {
        	severe("storage_write failed %s: %s", path, strerror(errno));
        	goto out;
            }	       
        }
	
	first_block_idx += nb_blocks_read_distant;
	    				  
    }	
    
    
    // Check if the initial local bins file size is bigger
    // than others bins files
    if (loc_file_init_blocks_nb > first_block_idx) {

        off_t length = first_block_idx * disk_block_size;
        ret = truncate(path, length);
        if (ret != 0) {
            severe("truncate(%s) failed: %s", path, strerror(errno));
            goto out;
        }
    }
        
    status = 0;
    			      
out:
    // This spare file used to exist but is not needed any more
    if (remove_file) {
        storage_rm_file(st, re->fid);
    }  
    // Nothing has been written to disk
    else if (access(path, F_OK) == -1) {
        storage_dev_map_distribution_remove(st, re->fid, 1/*spare*/);
    }
    
    
    for (i = 0; i < rozofs_safe; i++) {
        if (working_ctx.prj_ctx[i].bins) {
            free(working_ctx.prj_ctx[i].bins);
	    working_ctx.prj_ctx[i].bins = NULL;
	}	   
    }    
    if (fd != -1) {
      close(fd);
      fd = -1;
    }  
    if (working_ctx.data_read_p) {
      free(working_ctx.data_read_p);
      working_ctx.data_read_p = NULL;
    }  
    if (pforward) {
      free(pforward);
      pforward = NULL;
    }
    if (loc_read_bins_p) {
      free(loc_read_bins_p);
      loc_read_bins_p = NULL;
    }	
    return status;
}
   
int rbs_restore_one_rb_entry(storage_t * st, rb_entry_t * re, char * path, int device_id, uint8_t proj_id_to_rebuild) {
    int status = -1;
    int i = 0;
    int fd = -1;
    int ret = -1;
    uint8_t loc_file_exist = 1;
    struct stat loc_file_stat;
    uint32_t loc_file_init_blocks_nb = 0;
    bin_t * loc_read_bins_p = NULL;
    uint8_t version = 0;
    bid_t first_block_idx = 0;
    uint64_t file_size = 0;
    rbs_storcli_ctx_t working_ctx;
    int is_fid_faulty;

    // Get rozofs layout parameters
    uint8_t layout = re->layout;
    uint32_t bsize = re->bsize;
    uint16_t rozofs_disk_psize = rozofs_get_psizes(layout,bsize,proj_id_to_rebuild);
    uint16_t rozofs_max_psize = rozofs_get_max_psize(layout,bsize);
    uint32_t requested_blocks = ROZOFS_BLOCKS_IN_BUFFER(re->bsize);
    uint32_t nb_blocks_read_distant = requested_blocks;
    
    // Clear the working context
    memset(&working_ctx, 0, sizeof (rbs_storcli_ctx_t));

    // Check that this file already exists
    // on the storage to rebuild
    if (access(path, F_OK) == -1)
        loc_file_exist = 0;

    // If the local file exist
    // we must to check the nb. of blocks for this file
    if (loc_file_exist) {
        // Stat file
        if (stat(path, &loc_file_stat) != 0)
            goto out;
	if (!S_ISREG(loc_file_stat.st_mode)) {
	    severe("%s is %x", path, loc_file_stat.st_mode);
	    goto out;  	   	
	}
	if (loc_file_stat.st_size < ROZOFS_ST_BINS_FILE_HDR_SIZE) {
	    severe("%s has size %d", path, (int)loc_file_stat.st_size);
	    loc_file_exist = 0;  	   
	}
	else {
            // Compute the nb. of blocks
            loc_file_init_blocks_nb = (loc_file_stat.st_size -
                    ROZOFS_ST_BINS_FILE_HDR_SIZE) /
                    ((rozofs_max_psize * sizeof (bin_t))
                    + sizeof (rozofs_stor_bins_hdr_t));
	}  
    }

    // While we can read in the bins file
    while (nb_blocks_read_distant == requested_blocks) {

        // Clear the working context
        memset(&working_ctx, 0, sizeof (rbs_storcli_ctx_t));

        // Try to read blocks on others storages
        ret = rbs_read_blocks(re->storages, layout, bsize, st->cid,
                re->dist_set_current, re->fid, first_block_idx,
                requested_blocks, &nb_blocks_read_distant, retries,
                &working_ctx);

        if (ret != 0) {
            goto out;
        }

        if (nb_blocks_read_distant == 0)
            continue; // End of file


        if (first_block_idx == 0) {
            // Open local bins file for the first write
            fd = open(path, ROZOFS_ST_BINS_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
            if (fd < 0) {
                severe("open failed (%s) : %s", path, strerror(errno));
                goto out;
            }
        }

        if (stat(path, &loc_file_stat) != 0)
            goto out;

        // If projections to rebuild are not present on local file
        // - generate the projections to rebuild
        // - store projections on local bins file
        if (!loc_file_exist || loc_file_init_blocks_nb <= first_block_idx) {

            // Allocate memory for store projection
            working_ctx.prj_ctx[proj_id_to_rebuild].bins =
                    xmalloc((rozofs_max_psize * sizeof (bin_t) 
		    + sizeof (rozofs_stor_bins_hdr_t) + sizeof(rozofs_stor_bins_footer_t)) * 
		    nb_blocks_read_distant);

            memset(working_ctx.prj_ctx[proj_id_to_rebuild].bins, 0,
                    (rozofs_max_psize * sizeof (bin_t) +
                    sizeof (rozofs_stor_bins_hdr_t) + sizeof(rozofs_stor_bins_footer_t)) *
                    nb_blocks_read_distant);

            // Generate projections to rebuild
            ret = rbs_transform_forward_one_proj(
                    working_ctx.prj_ctx,
                    working_ctx.block_ctx_table,
                    layout,bsize,
                    0,
                    nb_blocks_read_distant,
                    proj_id_to_rebuild,
                    working_ctx.data_read_p);
            if (ret != 0) {
                severe("rbs_transform_forward_one_proj failed: %s",
                        strerror(errno));
                goto out;
            }

            // Check mtime of local file
            ret = ckeck_mtime(fd, loc_file_stat.st_mtim);
            if (ret != 0) {
                severe("rbs_restore_one_rb_entry failed:"
                        " concurrent access detected");
                goto out;
            }

            // Store the projections on local bins file	
            ret = storage_write(st, &device_id, layout, bsize, re->dist_set_current, 0/*spare*/,
                    re->fid, first_block_idx, nb_blocks_read_distant, version,
                    &file_size, working_ctx.prj_ctx[proj_id_to_rebuild].bins,
		    &is_fid_faulty);

            if (ret <= 0) {
                severe("storage_write failed: %s", strerror(errno));
                goto out;
            }

            // Update local file stat
            if (fstat(fd, &loc_file_stat) != 0)
                goto out;

            // Free projections generated
            free(working_ctx.prj_ctx[proj_id_to_rebuild].bins);
            working_ctx.prj_ctx[proj_id_to_rebuild].bins = NULL;

            // Free data read
            free(working_ctx.data_read_p);
            working_ctx.data_read_p = NULL;

            // Update the next first block to read
            first_block_idx += nb_blocks_read_distant;

            // Go to the next blocks
            continue;
        }

        loc_read_bins_p = NULL;
        size_t local_len_read = 0;
        uint32_t local_blocks_nb_read = 0;

        // If the bins file exist and the size is sufficient:
        if (loc_file_exist) {

            // Allocate memory for store local bins read
            loc_read_bins_p = xmalloc(nb_blocks_read_distant *
                    ((rozofs_max_psize * sizeof (bin_t))
                    + sizeof (rozofs_stor_bins_hdr_t)+ sizeof(rozofs_stor_bins_footer_t)));

            // Read local bins
            ret = storage_read(st, &device_id, layout, bsize, re->dist_set_current, 0/*spare*/, re->fid,
                    first_block_idx, nb_blocks_read_distant, loc_read_bins_p,
                    &local_len_read, &file_size, &is_fid_faulty);

            if (ret != 0) {
                severe("storage_read failed: %s", strerror(errno));
                goto out;
            }

            // Compute the nb. of local blocks read
            local_blocks_nb_read = local_len_read /
                    ((rozofs_max_psize * sizeof (bin_t))
                    + sizeof (rozofs_stor_bins_hdr_t)+ sizeof(rozofs_stor_bins_footer_t));

            // For each block read on distant storages
            for (i = 0; i < nb_blocks_read_distant; i++) {

                // If the block exist on local bins file
                if (i < local_blocks_nb_read) {

                    // Get pointer on current bins header
                    bin_t * current_loc_bins_p = loc_read_bins_p +
                            ((rozofs_max_psize +
                            ((sizeof (rozofs_stor_bins_hdr_t) + sizeof(rozofs_stor_bins_footer_t)) / sizeof (bin_t)))
                            * i);

                    rozofs_stor_bins_hdr_t * bins_hdr_local_p =
                            (rozofs_stor_bins_hdr_t *) current_loc_bins_p;
                    rozofs_stor_bins_footer_t * bins_foot_local_p = (rozofs_stor_bins_footer_t*)((bin_t*)(bins_hdr_local_p+1)+rozofs_get_psizes(layout,bsize,proj_id_to_rebuild));
                    // Compare timestamp of local and distant block
                    if ((bins_hdr_local_p->s.timestamp == bins_foot_local_p->timestamp) 
		    &&  (bins_hdr_local_p->s.timestamp == working_ctx.block_ctx_table[i].timestamp)) {
                        // The timestamp is the same on local
                        // Not need to generate a projection
                        if (DEBUG_RBS == 1) {
                            severe("SAME TS FOR BLOCK: %"PRIu64"",
                                    (first_block_idx + i));
                        }
                        continue; // Check next block
                    }
                }

                // The timestamp is not the same

                // Allocate memory for store projection
                working_ctx.prj_ctx[proj_id_to_rebuild].bins =
                        xmalloc((rozofs_max_psize * sizeof (bin_t) +
                        sizeof (rozofs_stor_bins_hdr_t)+sizeof(rozofs_stor_bins_footer_t)) *
                        nb_blocks_read_distant);

                memset(working_ctx.prj_ctx[proj_id_to_rebuild].bins, 0,
                        (rozofs_max_psize * sizeof (bin_t) +
                        sizeof (rozofs_stor_bins_hdr_t)+sizeof(rozofs_stor_bins_footer_t)) *
                        nb_blocks_read_distant);

                // Generate the nb_blocks_read projections
                ret = rbs_transform_forward_one_proj(
                        working_ctx.prj_ctx,
                        working_ctx.block_ctx_table,
                        layout, bsize,
                        i,
                        1,
                        proj_id_to_rebuild,
                        working_ctx.data_read_p);

                if (ret != 0) {
                    severe("rbs_transform_forward_one_proj failed: %s",
                            strerror(errno));
                    goto out;
                }

                // Check mtime of local file
                ret = ckeck_mtime(fd, loc_file_stat.st_mtim);
                if (ret != 0) {
                    severe("rbs_restore_one_rb_entry failed:"
                            " concurrent access detected");
                    goto out;
                }

                // warning("WRITE BLOCK: %lu", (first_block_idx + i));

                bin_t * bins_to_write =
                        working_ctx.prj_ctx[proj_id_to_rebuild].bins +
                        ((rozofs_max_psize + ((sizeof (rozofs_stor_bins_hdr_t)+sizeof(rozofs_stor_bins_footer_t))
                        / sizeof (bin_t))) * i);

                // Store the projections on local bins file	
                ret = storage_write(st, &device_id, layout, bsize, re->dist_set_current,
                        0/*spare*/, re->fid, first_block_idx + i, 1, version,
                        &file_size,bins_to_write,&is_fid_faulty);

                if (ret <= 0) {
                    severe("storage_write failed: %s", strerror(errno));
                    goto out;
                }

                // Update local file stat
                if (fstat(fd, &loc_file_stat) != 0) {
                    goto out;
                }

                // Free projections generated
                free(working_ctx.prj_ctx[proj_id_to_rebuild].bins);
                working_ctx.prj_ctx[proj_id_to_rebuild].bins = NULL;
            }
            // Free local bins read
            free(loc_read_bins_p);
            loc_read_bins_p = NULL;
        }

        // Update the first block to read
        first_block_idx += nb_blocks_read_distant;

        free(working_ctx.data_read_p);
        working_ctx.data_read_p = NULL;
    }

    // OK, clear pointers
    loc_read_bins_p = NULL;
    working_ctx.data_read_p = NULL;
    memset(&working_ctx, 0, sizeof (rbs_storcli_ctx_t));

    // Check if the initial local bins file size is bigger
    // than others bins files
    if (loc_file_exist && loc_file_init_blocks_nb > first_block_idx) {

        off_t length = first_block_idx * (rozofs_disk_psize * sizeof (bin_t) 
	             + sizeof (rozofs_stor_bins_hdr_t) + sizeof(rozofs_stor_bins_footer_t));

        // Check mtime of local file
        ret = ckeck_mtime(fd, loc_file_stat.st_mtim);
        if (ret != 0) {
            severe("rbs_restore_one_rb_entry failed:"
                    " concurrent access detected");
            goto out;
        }
        ret = ftruncate(fd, length);
        if (ret != 0) {
            severe("ftruncate failed: %s", strerror(errno));
            goto out;
        }
    }

    status = 0;
out:
    if (fd != -1)
        close(fd);
    if (working_ctx.prj_ctx[proj_id_to_rebuild].bins != NULL)
        free(working_ctx.prj_ctx[proj_id_to_rebuild].bins);
    if (working_ctx.data_read_p != NULL)
        free(working_ctx.data_read_p);
    if (loc_read_bins_p != NULL)
        free(loc_read_bins_p);
    return status;
}

extern sconfig_t storaged_config;
/** Initialize a storage to rebuild
 *
 * @param cid: unique id of cluster that owns this storage.
 * @param sid: the unique id for the storage to rebuild.
 * @param storage_root: the absolute path where rebuild bins file(s) 
 * will be store.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int rbs_initialize(cid_t cid, sid_t sid, const char *storage_root, 
                   uint32_t dev, uint32_t dev_mapper, uint32_t dev_red) {
    int status = -1;
    DEBUG_FUNCTION;

    // Initialize the storage to rebuild 
    if (storage_initialize(storage_to_rebuild, cid, sid, storage_root,
		dev,
		dev_mapper,
		dev_red) != 0)
        goto out;

    // Initialize the list of cluster(s)
    list_init(&cluster_entries);

    status = 0;
out:
    return status;
}

/** Initialize connections (via mproto and sproto) to one storage
 *
 * @param rb_stor: storage to connect.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int rbs_stor_cnt_initialize(rb_stor_t * rb_stor) {
    int status = -1;
    int i = 0;
    mp_io_address_t io_address[STORAGE_NODE_PORTS_MAX];
    DEBUG_FUNCTION;

    // Copy hostname for this storage
    strncpy(rb_stor->mclient.host, rb_stor->host, ROZOFS_HOSTNAME_MAX);
    memset(io_address, 0, STORAGE_NODE_PORTS_MAX * sizeof (mp_io_address_t));
    rb_stor->sclients_nb = 0;
 
    struct timeval timeo;
    timeo.tv_sec = RBS_TIMEOUT_MPROTO_REQUESTS;
    timeo.tv_usec = 0;

    // Initialize connection with this storage (by mproto)
    if (mclient_initialize(&rb_stor->mclient, timeo) != 0) {
        severe("failed to join storage (host: %s), %s.",
                rb_stor->host, strerror(errno));
        goto out;
    } else {
        // Send request to get TCP ports for this storage
        if (mclient_ports(&rb_stor->mclient, io_address) != 0) {
            severe("Warning: failed to get ports for storage (host: %s)."
                    , rb_stor->host);
            goto out;
        }
    }

    // Initialize each TCP ports connection with this storage (by sproto)
    for (i = 0; i < STORAGE_NODE_PORTS_MAX; i++) {
        if (io_address[i].port != 0) {

            struct timeval timeo;
            timeo.tv_sec = RBS_TIMEOUT_SPROTO_REQUESTS;
            timeo.tv_usec = 0;

            uint32_t ip = io_address[i].ipv4;
 
            if (ip == INADDR_ANY) {
                // Copy storage hostname and IP
                strcpy(rb_stor->sclients[i].host, rb_stor->host);
                rozofs_host2ip(rb_stor->host, &ip);
            } else {
                sprintf(rb_stor->sclients[i].host, "%u.%u.%u.%u", ip >> 24,
                        (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
            }

            rb_stor->sclients[i].ipv4 = ip;
            rb_stor->sclients[i].port = io_address[i].port;
            rb_stor->sclients[i].status = 0;
            rb_stor->sclients[i].rpcclt.sock = -1;

            if (sclient_initialize(&rb_stor->sclients[i], timeo) != 0) {
                severe("failed to join storage (host: %s, port: %u), %s.",
                        rb_stor->host, rb_stor->sclients[i].port,
                        strerror(errno));
                goto out;
            }
            rb_stor->sclients_nb++;
        }
    }

    status = 0;
out:
    return status;
}

/** Retrieves the list of bins files to rebuild from a storage
 *
 * @param rb_stor: storage contacted.
 * @param cid: unique id of cluster that owns this storage.
 * @param sid: the unique id for the storage to rebuild.
 *
 * @return: 0  -1 otherwise (errno is set)
 */
int rbs_get_rb_entry_list_one_storage(rb_stor_t *rb_stor, cid_t cid,
        sid_t sid, int parallel, int *cfgfd) {
    int status = -1;
    uint16_t slice = 0;
    uint8_t spare = 0;
    uint8_t device = 0;
    uint64_t cookie = 0;
    uint8_t eof = 0;
    sid_t dist_set[ROZOFS_SAFE_MAX];
    bins_file_rebuild_t * children = NULL;
    bins_file_rebuild_t * iterator = NULL;
    bins_file_rebuild_t * free_it = NULL;
    int            idx;
    int            ret;
    rozofs_rebuild_entry_file_t file_entry;
    
    DEBUG_FUNCTION;


    idx = 0;
  
    memset(dist_set, 0, sizeof (sid_t) * ROZOFS_SAFE_MAX);

    // While the end of the list is not reached
    while (eof == 0) {

        // Send a request to storage to get the list of bins file(s)
        if (rbs_get_rb_entry_list(&rb_stor->mclient, cid, rb_stor->sid, sid,
                &device, &spare, &slice, &cookie, &children, &eof) != 0) {
            severe("rbs_get_rb_entry_list failed: %s\n", strerror(errno));
            goto out;
        }

        iterator = children;

        // For each entry 
        while (iterator != NULL) {

           // Verify if this entry is already present in list
	    if (rb_hash_table_search(iterator->fid) == 0) { 
	    
                rb_hash_table_insert(iterator->fid);
			
		memcpy(file_entry.fid,iterator->fid, sizeof (fid_t));
		file_entry.layout = iterator->layout;
		file_entry.bsize  = iterator->bsize;
        	file_entry.todo   = 1;    
	        file_entry.unlink = 0;       
        	memcpy(file_entry.dist_set_current, iterator->dist_set_current, sizeof (sid_t) * ROZOFS_SAFE_MAX);	    

        	ret = write(cfgfd[idx],&file_entry,sizeof(file_entry)); 
		if (ret != sizeof(file_entry)) {
		  severe("can not write file cid%d sid%d %d %s",cid,sid,idx,strerror(errno));
		}	    
		idx++;
		if (idx >= parallel) idx = 0; 		
		
            }

            free_it = iterator;
            iterator = iterator->next;
            free(free_it);
        }
      }

    status = 0;
out:      


    return status;
}

/** Check if the storage is present on cluster list
 *
 * @param cluster_entries: list of cluster(s).
 * @param cid: unique id of cluster that owns this storage.
 * @param sid: the unique id for the storage to rebuild.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int rbs_check_cluster_list(list_t * cluster_entries, cid_t cid, sid_t sid) {
    list_t *p, *q;

    list_for_each_forward(p, cluster_entries) {

        rb_cluster_t *clu = list_entry(p, rb_cluster_t, list);

        if (clu->cid == cid) {

            list_for_each_forward(q, &clu->storages) {

                rb_stor_t *stor = list_entry(q, rb_stor_t, list);

                // Check if the sid to rebuild exist in the list
                if (stor->sid == sid)
                    return 0;
            }
        }
    }
    errno = EINVAL;
    return -1;
}

/** Init connections for storage members of a given cluster but not for the 
 *  storage with sid=sid
 *
 * @param cluster_entries: list of cluster(s).
 * @param cid: unique id of cluster that owns this storage.
 * @param sid: the unique id for the storage to rebuild.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int rbs_init_cluster_cnts(list_t * cluster_entries, cid_t cid,
        sid_t sid) {
    list_t *p, *q;
    int status = -1;

    list_for_each_forward(p, cluster_entries) {

        rb_cluster_t *clu = list_entry(p, rb_cluster_t, list);

        if (clu->cid == cid) {

            list_for_each_forward(q, &clu->storages) {

                rb_stor_t *rb_stor = list_entry(q, rb_stor_t, list);

                if (rb_stor->sid == sid)
                    continue;

                // Get connections for this storage
                if (rbs_stor_cnt_initialize(rb_stor) != 0) {
                    severe("rbs_stor_cnt_initialize cid/sid %d/%d failed: %s",
                            cid, rb_stor->sid, strerror(errno));
                    goto out;
                }
            }
        }
    }

    status = 0;
out:
    return status;
}

/** Release the list of cluster(s)
 *
 * @param cluster_entries: list of cluster(s).
 */
static void rbs_release_cluster_list(list_t * cluster_entries) {
    list_t *p, *q, *r, *s;
    int i = 0;

    list_for_each_forward_safe(p, q, cluster_entries) {

        rb_cluster_t *clu = list_entry(p, rb_cluster_t, list);

        list_for_each_forward_safe(r, s, &clu->storages) {

            rb_stor_t *rb_stor = list_entry(r, rb_stor_t, list);

            // Remove and free storage
            mclient_release(&rb_stor->mclient);

            for (i = 0; i < rb_stor->sclients_nb; i++)
                sclient_release(&rb_stor->sclients[i]);

            list_remove(&rb_stor->list);
            free(rb_stor);

        }

        // Remove and free cluster
        list_remove(&clu->list);
        free(clu);
    }
}

/** Retrieves the list of bins files to rebuild for a given storage
 *
 * @param cluster_entries: list of cluster(s).
 * @param cid: unique id of cluster that owns this storage.
 * @param sid: the unique id for the storage to rebuild.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
static int rbs_get_rb_entry_list_one_cluster(list_t * cluster_entries,
        cid_t cid, sid_t sid, int parallel) {
    list_t       *p, *q;
    int            status = -1;
    char         * dir;
    char           filename[FILENAME_MAX];
    int            idx;
    int            cfgfd[MAXIMUM_PARALLEL_REBUILD_PER_SID];
    int            ret;
        
    /*
    ** Create FID list file files
    */
    dir = get_rebuild_directory_name();
    for (idx=0; idx < parallel; idx++) {
    
      sprintf(filename,"%s/c%d_s%d_dall.%2.2d", dir, cid, sid, idx);

      cfgfd[idx] = open(filename,O_CREAT | O_TRUNC | O_WRONLY, 0640);
      if (cfgfd[idx] == -1) {
	severe("Can not open file %s %s", filename, strerror(errno));
	return -1;
      }

      ret = write(cfgfd[idx],&st2rebuild,sizeof(st2rebuild));
      if (ret != sizeof(st2rebuild)) {
	severe("Can not write header in file %s %s", filename, strerror(errno));
	return -1;      
      }
    }    


    list_for_each_forward(p, cluster_entries) {

        rb_cluster_t *clu = list_entry(p, rb_cluster_t, list);

        if (clu->cid == cid) {

            list_for_each_forward(q, &clu->storages) {

                rb_stor_t *rb_stor = list_entry(q, rb_stor_t, list);

                if (rb_stor->sid == sid)
                    continue;

                // Get the list of bins files to rebuild for this storage
                if (rbs_get_rb_entry_list_one_storage(rb_stor, cid, sid, parallel,cfgfd) != 0) {

                    severe("rbs_get_rb_entry_list_one_storage failed: %s\n",
                            strerror(errno));
                    goto out;
                }
            }
        }
    }

    status = 0;
out:
    for (idx=0; idx < parallel; idx++) {
      close(cfgfd[idx]);
    }  
    return status;
}

/** Build a list with just one FID
 *
 * @param cid: unique id of cluster that owns this storage.
 * @param sid: the unique id for the storage to rebuild.
 * @param fid2rebuild: the FID to rebuild
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
static int rbs_build_one_fid_list(cid_t cid, sid_t sid, uint8_t layout, uint8_t bsize, uint8_t * dist, fid_t fid2rebuild) {
  int            fd; 
  rozofs_rebuild_entry_file_t file_entry;
  char         * dir;
  char           filename[FILENAME_MAX];
  int            ret;
  int            i;
  
  /*
  ** Create FID list file files
  */
  dir = get_rebuild_directory_name();

  sprintf(filename,"%s/c%d_s%d", dir, cid, sid);
      
  fd = open(filename,O_CREAT | O_TRUNC | O_WRONLY, 0640);
  if (fd == -1) {
    severe("Can not open file %s %s", filename, strerror(errno));
    return -1;
  }
    
  ret = write(fd,&st2rebuild,sizeof(st2rebuild));
  if (ret != sizeof(st2rebuild)) {
    severe("Can not write header in file %s %s", filename, strerror(errno));
    return -1;      
  }  

  memcpy(file_entry.fid,fid2rebuild, sizeof (fid_t));
  file_entry.layout = layout;
  file_entry.bsize  = bsize;  
  file_entry.todo   = 1;      
  file_entry.unlink = 1;      
  
  
  for(i=0; i<ROZOFS_SAFE_MAX; i++) {
    file_entry.dist_set_current[i] = dist[i];
  }    

  ret = write(fd,&file_entry,sizeof(file_entry)); 
  if (ret != sizeof(file_entry)) {
    severe("can not write file cid%d sid%d %s",cid,sid,strerror(errno));
    return -1;
  }  
  
  close(fd);
  return 0;   
}
/** Retrieves the list of bins files to rebuild from the available disks
 *
 * @param cluster_entries: list of cluster(s).
 * @param cid: unique id of cluster that owns this storage.
 * @param sid: the unique id for the storage to rebuild.
 * @param device: the missing device identifier 
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
static int rbs_build_device_missing_list_one_cluster(cid_t cid, 
						     sid_t sid,
						     int device_to_rebuild,
						     int parallel) {
  char           dir_path[FILENAME_MAX];						     
  char           slicepath[FILENAME_MAX];						     
  char           filepath[FILENAME_MAX];						     
  int            device_it;
  int            spare_it;
  DIR           *dir1;
  struct dirent *file;
  int            fd; 
  size_t         nb_read;
  rozofs_stor_bins_file_hdr_t file_hdr; 
  rozofs_rebuild_entry_file_t file_entry;
  int            idx;
  char         * dir;
  char           filename[FILENAME_MAX];
  int            cfgfd[MAXIMUM_PARALLEL_REBUILD_PER_SID];
  int            ret;
  int            slice;
  
  /*
  ** Create FID list file files
  */
  dir = get_rebuild_directory_name();
  for (idx=0; idx < parallel; idx++) {

    sprintf(filename,"%s/c%d_s%d_d%d.%2.2d", dir, cid, sid, device_to_rebuild, idx);
      
    cfgfd[idx] = open(filename,O_CREAT | O_TRUNC | O_WRONLY, 0640);
    if (cfgfd[idx] == -1) {
      severe("Can not open file %s %s", filename, strerror(errno));
      return 0;
    }
    
    ret = write(cfgfd[idx],&st2rebuild,sizeof(st2rebuild));
    if (ret != sizeof(st2rebuild)) {
      severe("Can not write header in file %s %s", filename, strerror(errno));
      return 0;      
    }
  }    
  
  idx = 0;
  

  // Loop on all the devices
  for (device_it = 0; device_it < storage_to_rebuild->device_number;device_it++) {

    // Do not read the disk to rebuild
    if (device_it == device_to_rebuild) continue;

    // For spare and no spare
    for (spare_it = 0; spare_it < 2; spare_it++) {

      // Build path directory for this layout and this spare type        	
      sprintf(dir_path, "%s/%d/hdr_%u", storage_to_rebuild->root, device_it, spare_it);

      // Check that this directory already exists, otherwise it will be create
      if (access(dir_path, F_OK) == -1) continue;

      for (slice=0; slice < FID_STORAGE_SLICE_SIZE; slice++) {

        storage_build_hdr_path(slicepath, storage_to_rebuild->root, device_it, spare_it, slice);

        // Open this directory
        dir1 = opendir(slicepath);
        if (dir1 == NULL) continue;


        // Loop on header files in slice directory
        while ((file = readdir(dir1)) != NULL) {
          int i;

          // Read the file
          sprintf(filepath, "%s/%s",slicepath, file->d_name);

	      fd = open(filepath, ROZOFS_ST_NO_CREATE_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
	      if (fd < 0) continue;

          nb_read = pread(fd, &file_hdr, sizeof(file_hdr), 0);
	      close(fd);	    

          // What to do with such an error ?
	      if (nb_read != sizeof(file_hdr)) continue;

	      // Check whether this file should have a header(mapper) file on 
          // the device we are rebuilding
          for (i=0; i < storage_to_rebuild->mapper_redundancy; i++) {
	        int dev;

            dev = storage_mapper_device(file_hdr.fid,i,storage_to_rebuild->mapper_modulo);

 	        if (dev == device_to_rebuild) {
	          // Let's re-write the header file  
              storage_build_hdr_path(filepath, storage_to_rebuild->root, device_to_rebuild, spare_it, slice);
              storage_write_header_file(NULL,dev,filepath,&file_hdr);	
	          break;
	        } 
	       }  

          // Not a file whose data part stand on the device to rebuild
          if (file_hdr.device_id != device_to_rebuild) continue;

          // Check whether this FID is already set in the list
	      if (rb_hash_table_search(file_hdr.fid) != 0) continue;

	      rb_hash_table_insert(file_hdr.fid);	    

	      memcpy(file_entry.fid,file_hdr.fid, sizeof (fid_t));
	      file_entry.layout = file_hdr.layout;
	      file_entry.bsize  = file_hdr.bsize;
              file_entry.todo   = 1;     
	      file_entry.unlink = 0;       
              memcpy(file_entry.dist_set_current, file_hdr.dist_set_current, sizeof (sid_t) * ROZOFS_SAFE_MAX);	    

              ret = write(cfgfd[idx],&file_entry,sizeof(file_entry)); 
	      if (ret != sizeof(file_entry)) {
	        severe("can not write file cid%d sid%d %d %s",cid,sid,idx,strerror(errno));
	      }

	      idx++;
	      if (idx >= parallel) idx = 0; 

	    } // End of loop in one slice 
	    closedir(dir1);  
      } // End of slices
    }
  } 

  for (idx=0; idx < parallel; idx++) {
    close(cfgfd[idx]);
  }  
  return 0;   
}
/** Remove empty rebuild list files 
 *
 */
static int rbs_do_remove_lists() {
  char         * dirName;
  char           fileName[FILENAME_MAX];
  DIR           *dir;
  struct dirent *file;
    
  /*
  ** Start one rebuild process par rebuild file
  */
  dirName = get_rebuild_directory_name();
  
  /*
  ** Open this directory
  */
  dir = opendir(dirName);
  if (dir == NULL) {
    if (errno == ENOENT) return 0;
    severe("opendir(%s) %s", dirName, strerror(errno));
    return -1;
  } 	  
  /*
  ** Loop on distibution sub directories
  */
  while ((file = readdir(dir)) != NULL) {  
    if (strcmp(file->d_name,".")==0)  continue;
    if (strcmp(file->d_name,"..")==0) continue;
    sprintf(fileName,"%s/%s",dirName,file->d_name);
    unlink(fileName);
  }
  
  closedir(dir);
  return 0;
} 
/** Rebuild list just produced 
 *
 */
static int rbs_do_list_rebuild() {
  char         * dirName;
  char           cmd[FILENAME_MAX];
  DIR           *dir;
  struct dirent *file;
  int            total;
  int            status;
  int            failure;
  int            success;
    
  /*
  ** Start one rebuild process par rebuild file
  */
  dirName = get_rebuild_directory_name();
  
  /*
  ** Open this directory
  */
  dir = opendir(dirName);
  if (dir == NULL) {
    if (errno == ENOENT) return 0;
    severe("opendir(%s) %s", dirName, strerror(errno));
    return -1;
  } 	  
  /*
  ** Loop on distibution sub directories
  */
  total = 0;
  while ((file = readdir(dir)) != NULL) {
    pid_t pid;
  
    if (strcmp(file->d_name,".")==0)  continue;
    if (strcmp(file->d_name,"..")==0) continue;
    

    pid = fork();
    
    if (pid == 0) {
      sprintf(cmd,"storage_list_rebuilder -f %s/%s", dirName, file->d_name);
      status = system(cmd);
      if (status == 0) exit(0);
      exit(-1);
    }
      
    total++;
    
  }
  closedir(dir);
  
  failure = 0;
  success = 0;
  while (total > (failure+success)) {
  
     
    if (waitpid(-1,&status,0) == -1) {
      severe("waitpid %s",strerror(errno));
    }
    status = WEXITSTATUS(status);
    if (status != 0) failure++;
    else             success++;
  }
  
  if (failure != 0) {
    severe("%d list rebuild processes failed upon %d",failure,total);
    return -1;
  }
  return 0;
}

int rbs_sanity_check(const char *export_host_list, int site, cid_t cid, sid_t sid,
        const char *root, uint32_t dev, uint32_t dev_mapper, uint32_t dev_red) {

    int status = -1;
    char * pExport_host = 0;

    DEBUG_FUNCTION;

    // Try to initialize the storage to rebuild
    if (rbs_initialize(cid, sid, root, dev, dev_mapper, dev_red) != 0) {
        // Probably a path problem
        fprintf(stderr, "Can't initialize rebuild storage (cid:%u; sid:%u;"
                " path:%s): %s\n", cid, sid, root, strerror(errno));
        goto out;
    }
    
    // Try to get the list of storages for this cluster ID
    pExport_host = rbs_get_cluster_list(&rpcclt_export, export_host_list, 
                                        site, cid, &cluster_entries);
    if (pExport_host == NULL) {	    
        fprintf(stderr, "Can't get list of others cluster members from export"
                " server (%s) for storage to rebuild (cid:%u; sid:%u): %s\n",
                export_host_list, cid, sid, strerror(errno));
        goto out;
    }

    // Check the list of cluster
    if (rbs_check_cluster_list(&cluster_entries, cid, sid) != 0) {
        fprintf(stderr, "The storage to rebuild with sid=%u is not present in"
                " cluster with cid=%u\n", sid, cid);
        goto out;
    }

    status = 0;

out:
    // Free cluster(s) list
    rbs_release_cluster_list(&cluster_entries);

    return status;
}

int rbs_rebuild_storage(const char *export_host_list, int site, cid_t cid, sid_t sid,
        const char *root, uint32_t dev, uint32_t dev_mapper, uint32_t dev_red,
	uint8_t stor_idx, int device,
	int parallel, char * config_file, 
	fid_t fid2rebuild) {
    int status = -1;
    int ret;
    char * pExport_host = 0;

    DEBUG_FUNCTION;

    rb_hash_table_initialize();

    // Initialize the storage to rebuild
    if (rbs_initialize(cid, sid, root, dev, dev_mapper, dev_red) != 0) {
        severe("can't init. storage to rebuild (cid:%u;sid:%u;path:%s)",
                cid, sid, root);
        goto out;
    }
    strcpy(st2rebuild.export_hostname,export_host_list);
    strcpy(st2rebuild.config_file,config_file);
    st2rebuild.site = site;


    // Get the list of storages for this cluster ID
    pExport_host = rbs_get_cluster_list(&rpcclt_export, export_host_list, 
                                        site, cid, &cluster_entries);
    if (pExport_host == NULL) {					
        severe("rbs_get_cluster_list failed (cid: %u) : %s", cid, strerror(errno));
        goto out;
    }

    // Check the list of cluster
    if (rbs_check_cluster_list(&cluster_entries, cid, sid) != 0)
        goto out;

    // Get connections for this given cluster
    if (rbs_init_cluster_cnts(&cluster_entries, cid, sid) != 0)
        goto out;
	
    // Remove any old files that should exist in the process directory
    ret = rbs_do_remove_lists();

    // One FID to rebuild
    if (device == -2) {
      uint32_t   bsize;
      uint8_t    layout; 
      ep_mattr_t attr;
      
      // Resolve this FID thanks to the exportd
      if (rbs_get_fid_attr(&rpcclt_export, pExport_host, fid2rebuild, &attr, &bsize, &layout) != 0)
      {
        if (errno == ENOENT) {
	  status = -2;
          fprintf(stderr, "Storage rebuild failed !\nUnknown FID\n");
	}
	else {
          severe("Can not get attributes from export %s",strerror(errno));
	}
	goto out;
      }
      
      if (rbs_build_one_fid_list(cid, sid, layout, bsize, attr.sids, fid2rebuild) != 0)
        goto out;
      rb_fid_table_count = 1;	
      parallel           = 1; 
    }
    else if (device == -1) {
      // Build the list from the remote storages
      if (rbs_get_rb_entry_list_one_cluster(&cluster_entries, cid, sid, parallel) != 0)
        goto out;  	 	 	 
    }
    else {
      // Is it the only device because in such a case it is a total rebuild
      if (device >= st2rebuild.storage.device_number) {
        REBUILD_MSG("storaged_rebuild failed ! No such device number %d.\n",device);
	goto out;
      }
      if (st2rebuild.storage.device_number == 1) {
	// Build the list from the remote storages
	if (rbs_get_rb_entry_list_one_cluster(&cluster_entries, cid, sid, parallel) != 0)
          goto out;         
      }
      else {
	// Build the list from the available data on local disk
	if (rbs_build_device_missing_list_one_cluster(cid, sid, device, parallel) != 0)
          goto out;
      }	  	    		
    }
    
    rb_hash_table_delete();

    // Free cluster(s) list
    rbs_release_cluster_list(&cluster_entries);
    
    status = 0;


    // Actually process the rebuild
    if (rb_fid_table_count==0) {
      REBUILD_MSG("No file to rebuild");
      ret = rbs_do_remove_lists();
    }
    else {
      REBUILD_MSG("%llu files to rebuild by %d processes",
           (unsigned long long int)rb_fid_table_count,parallel);
      ret = rbs_do_list_rebuild();
      while (ret != 0) {
	REBUILD_MSG("Rebuild failed. Will retry within %d seconds", TIME_BETWEEN_2_RB_ATTEMPS);
	sleep(TIME_BETWEEN_2_RB_ATTEMPS); 
	ret = rbs_do_list_rebuild();
      }
    }
    
    return status;
    
out:
    rb_hash_table_delete();

    // Free cluster(s) list
    rbs_release_cluster_list(&cluster_entries);
 
    return status;
}

