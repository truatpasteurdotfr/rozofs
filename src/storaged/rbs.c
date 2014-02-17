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

#include <rozofs/common/list.h>
#include <rozofs/common/htable.h>
#include <rozofs/rozofs.h>
#include <rozofs/rozofs_srv.h>
#include <rozofs/rpc/rpcclt.h>
#include <rozofs/rpc/mclient.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/spproto.h>
#include <rozofs/core/rozofs_host2ip.h>

#include "sconfig.h"
#include "storage.h"
#include "rbs_sclient.h"
#include "rbs_eclient.h"
#include "rbs.h"

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

static unsigned int fid_hash(void *key) {
    uint32_t hash = 0;
    uint8_t *c;
    for (c = key; c != key + 16; c++)
        hash = *c + (hash << 6) + (hash << 16) - hash;
    return hash;
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
#define FID_TABLE_HASH_SIZE  (64*1024)

#define FID_MAX_ENTRY      511
typedef struct _rb_fid_entries_t {
    int                        count;
    struct _rb_fid_entries_t * next;   
    fid_t                      fid[FID_MAX_ENTRY];
} rb_fid_entries_t;

rb_fid_entries_t ** rb_fid_table=NULL;
uint64_t            rb_fid_table_count=0;


/*
**
*/
void rb_hash_table_initialize() {
  rb_fid_table = malloc(sizeof(rb_fid_entries_t)*FID_TABLE_HASH_SIZE);
  memset(rb_fid_table,0,sizeof(rb_fid_table));
  rb_fid_table_count = 0;
}

int rb_hash_table_search(fid_t fid) {
  int      i;
  uint16_t idx = (fid[0]<<8) + fid[1];
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
  uint16_t idx = (fid[0]<<8) + fid[1];
  rb_fid_entries_t * p;
  
  p = rb_hash_table_get(idx);
  memcpy(p->fid[p->count],fid,sizeof(fid_t));
  p->count++;
  rb_fid_table_count++;
}
int rb_hash_table_delete() {
  int idx;
  rb_fid_entries_t * p, * pNext;
  
  for (idx = 0; idx < FID_TABLE_HASH_SIZE; idx++) {
    
    p = rb_fid_table[idx];
    while (p != NULL) {
      pNext = p->next;
      free(p);
      p = pNext;
    }
  }
  
  free(rb_fid_table);
}





/** Get name of temporary rebuild directory
 *
 */
char rebuild_directory_name[FILENAME_MAX];
char * get_rebuild_directory_name() {
  pid_t pid = getpid();
  sprintf(rebuild_directory_name,"/tmp/rebuild.%d",pid);  
  return rebuild_directory_name;
}

int rbs_restore_one_rb_entry(storage_t * st, rb_entry_t * re) {
    int status = -1;
    int i = 0;
    char path[FILENAME_MAX];
    int fd = -1;
    uint8_t spare = 0;
    int ret = -1;
    uint8_t loc_file_exist = 1;
    struct stat loc_file_stat;
    uint32_t loc_file_init_blocks_nb = 0;
    bin_t * loc_read_bins_p = NULL;
    uint8_t version = 0;
    tid_t proj_id_to_rebuild = 0;
    uint32_t nb_blocks_read_distant = ROZOFS_BLOCKS_MAX;
    bid_t first_block_idx = 0;
    uint64_t file_size = 0;
    rbs_storcli_ctx_t working_ctx;
    int device_id;

    // Get rozofs layout parameters
    uint8_t layout = re->layout;
    uint8_t rozofs_safe = rozofs_get_rozofs_safe(layout);
    uint8_t rozofs_forward = rozofs_get_rozofs_forward(layout);
    uint16_t rozofs_max_psize = rozofs_get_max_psize(layout);

    // Clear the working context
    memset(&working_ctx, 0, sizeof (rbs_storcli_ctx_t));

    // Compute the proj_id to rebuild
    // Check if the storage to rebuild is
    // a spare for this entry
    for (i = 0; i < rozofs_safe; i++) {
        if (re->dist_set_current[i] == st->sid) {
            proj_id_to_rebuild = i;
            if (i >= rozofs_forward) {
                spare = 1;
                status = 0;
                goto out;
            }
        }
    }

    // Build the full path of directory that contains the bins file
    device_id = -1; // The device must be allocated
    if (storage_dev_map_distribution(DEVICE_MAP_SEARCH_CREATE, st, &device_id,
                                     re->fid, re->layout, re->dist_set_current, spare,
                                     path, version) == NULL) {
      severe("rbs_restore_one_rb_entry storage_dev_map_distribution");
      goto out;      
    }  

    // Check that this directory already exists, otherwise it will be create
    if (access(path, F_OK) == -1) {
        if (errno == ENOENT) {
            // If the directory doesn't exist, create it
            if (mkdir(path, ROZOFS_ST_DIR_MODE) != 0) {
                severe("mkdir failed (%s) : %s", path, strerror(errno));
                goto out;
            }
        } else {
            goto out;
        }
    }

    // Build the path of bins file
    storage_map_projection(re->fid, path);

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
        // Compute the nb. of blocks
        loc_file_init_blocks_nb = (loc_file_stat.st_size) /
                ((rozofs_max_psize * sizeof (bin_t)) + sizeof (rozofs_stor_bins_hdr_t));
    }

    // While we can read in the bins file
    while (nb_blocks_read_distant == ROZOFS_BLOCKS_MAX) {

        // Clear the working context
        memset(&working_ctx, 0, sizeof (rbs_storcli_ctx_t));

        // Try to read blocks on others storages
        ret = rbs_read_blocks(re->storages, layout, st->cid,
                re->dist_set_current, re->fid, first_block_idx,
                ROZOFS_BLOCKS_MAX, &nb_blocks_read_distant, retries,
                &working_ctx);

        if (ret != 0) {
            severe("rbs_read_blocks failed for block %"PRIu64": %s",
                    first_block_idx, strerror(errno));
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
                    xmalloc((rozofs_max_psize * sizeof (bin_t) +
                    sizeof (rozofs_stor_bins_hdr_t)) * nb_blocks_read_distant);

            memset(working_ctx.prj_ctx[proj_id_to_rebuild].bins, 0,
                    (rozofs_max_psize * sizeof (bin_t) +
                    sizeof (rozofs_stor_bins_hdr_t)) *
                    nb_blocks_read_distant);

            // Generate projections to rebuild
            ret = rbs_transform_forward_one_proj(
                    working_ctx.prj_ctx,
                    working_ctx.block_ctx_table,
                    layout,
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
            ret = storage_write(st, &device_id, layout, re->dist_set_current, spare,
                    re->fid, first_block_idx, nb_blocks_read_distant, version,
                    &file_size, working_ctx.prj_ctx[proj_id_to_rebuild].bins);

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
                    + sizeof (rozofs_stor_bins_hdr_t)));

            // Read local bins
            ret = storage_read(st, &device_id, layout, re->dist_set_current, spare, re->fid,
                    first_block_idx, nb_blocks_read_distant, loc_read_bins_p,
                    &local_len_read, &file_size);

            if (ret != 0) {
                severe("storage_read failed: %s", strerror(errno));
                goto out;
            }

            // Compute the nb. of local blocks read
            local_blocks_nb_read = local_len_read /
                    ((rozofs_max_psize * sizeof (bin_t))
                    + sizeof (rozofs_stor_bins_hdr_t));

            // For each block read on distant storages
            for (i = 0; i < nb_blocks_read_distant; i++) {

                // If the block exist on local bins file
                if (i < local_blocks_nb_read) {

                    // Get pointer on current bins header
                    bin_t * current_loc_bins_p = loc_read_bins_p +
                            ((rozofs_max_psize +
                            (sizeof (rozofs_stor_bins_hdr_t) / sizeof (bin_t)))
                            * i);

                    rozofs_stor_bins_hdr_t * bins_hdr_local_p =
                            (rozofs_stor_bins_hdr_t *) current_loc_bins_p;

                    // Compare timestamp of local and distant block
                    if (bins_hdr_local_p->s.timestamp ==
                            working_ctx.block_ctx_table[i].timestamp) {
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
                        sizeof (rozofs_stor_bins_hdr_t)) *
                        nb_blocks_read_distant);

                memset(working_ctx.prj_ctx[proj_id_to_rebuild].bins, 0,
                        (rozofs_max_psize * sizeof (bin_t) +
                        sizeof (rozofs_stor_bins_hdr_t)) *
                        nb_blocks_read_distant);

                // Generate the nb_blocks_read projections
                ret = rbs_transform_forward_one_proj(
                        working_ctx.prj_ctx,
                        working_ctx.block_ctx_table,
                        layout,
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
                        ((rozofs_max_psize + (sizeof (rozofs_stor_bins_hdr_t)
                        / sizeof (bin_t))) * i);

                // Store the projections on local bins file	
                ret = storage_write(st, &device_id, layout, re->dist_set_current,
                        spare, re->fid, first_block_idx + i, 1, version,
                        &file_size,
                        bins_to_write);

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

        off_t length = first_block_idx * (rozofs_max_psize * sizeof (bin_t) 
	             + sizeof (rozofs_stor_bins_hdr_t));

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
int rbs_initialize(cid_t cid, sid_t sid, const char *storage_root) {
    int status = -1;
    DEBUG_FUNCTION;

    // Initialize the storage to rebuild 
    if (storage_initialize(storage_to_rebuild, cid, sid, storage_root,
		storaged_config.device.total,
		storaged_config.device.mapper,
		storaged_config.device.redundancy) != 0)
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
    uint8_t layout = 0;
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
                &device, &spare, &layout, dist_set, &cookie, &children, &eof) != 0) {
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
        	file_entry.todo = 1;      
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
                    severe("rbs_stor_cnt_initialize failed: %s",
                            strerror(errno));
                    goto out;
                }
            }
        }
    }

    status = 0;
out:
    return status;
}

/** Release storages connections of cluster(s)
 *
 * @param cluster_entries: list of cluster(s).
 */
static void rbs_release_cluster_cnts(list_t * cluster_entries) {
    list_t *p, *q, *r, *s;
    int i = 0;

    list_for_each_forward_safe(p, q, cluster_entries) {

        rb_cluster_t *clu = list_entry(p, rb_cluster_t, list);

        list_for_each_forward_safe(r, s, &clu->storages) {

            rb_stor_t *rb_stor = list_entry(r, rb_stor_t, list);

            // Remove cnts fot this storage
            mclient_release(&rb_stor->mclient);

            for (i = 0; i < rb_stor->sclients_nb; i++)
                sclient_release(&rb_stor->sclients[i]);
        }
    }
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
    int            cfgfd[RBS_MAX_PARALLEL];
    int            ret;
        
    /*
    ** Create FID list file files
    */
    dir = get_rebuild_directory_name();
    for (idx=0; idx < parallel; idx++) {
    
      sprintf(filename,"%s/cid_%d_sid_%d_dev_all.it%d", dir, cid, sid, idx);

      cfgfd[idx] = open(filename,O_CREAT | O_TRUNC | O_WRONLY);
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

/** Retrieves the list of bins files to rebuild from the available disks
 *
 * @param cluster_entries: list of cluster(s).
 * @param cid: unique id of cluster that owns this storage.
 * @param sid: the unique id for the storage to rebuild.
 * @param device: the missing device identifier 
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
static int rbs_build_device_missing_list_one_cluster(list_t * cluster_entries,
                                                     cid_t cid, 
						     sid_t sid,
						     int device_to_rebuild,
						     int parallel) {
  char           path[FILENAME_MAX];						     
  int            device_it;
  int            layout_it;
  int            spare_it;
  DIR           *dir1;
  struct dirent *distrib;    
  DIR           *dir2;
  struct dirent *file;
  int            fd; 
  size_t         nb_read;
  rozofs_stor_bins_file_hdr_t file_hdr; 
  rozofs_rebuild_entry_file_t file_entry;
  rb_entry_t    *nre;
  int            idx;
  char         * dir;
  char           filename[FILENAME_MAX];
  int            cfgfd[RBS_MAX_PARALLEL];
  int            ret;
  
  /*
  ** Create FID list file files
  */
  dir = get_rebuild_directory_name();
  for (idx=0; idx < parallel; idx++) {

    sprintf(filename,"%s/cid_%d_sid_%d_dev_%d.it%d", dir, cid, sid, device_to_rebuild, idx);
      
    cfgfd[idx] = open(filename,O_CREAT | O_TRUNC | O_WRONLY);
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

    // For each possible layout
    for (layout_it = 0; layout_it < LAYOUT_MAX; layout_it++) {

      // For spare and no spare
      for (spare_it = 0; spare_it < 2; spare_it++) {

        // Build path directory for this layout and this spare type        	
        sprintf(path, "%s/%d/layout_%u/spare_%u/", 
		storage_to_rebuild->root, device_it, layout_it, spare_it);

        // Open this directory
        dir1 = opendir(path);
	if (dir1 == NULL) continue;

	// Loop on distibution sub directories
	while ((distrib = readdir(dir1)) != NULL) {

          // Do not process . and .. entries
          if (distrib->d_name[0] == '.') {
	    if (distrib->d_name[1] == 0) continue;
	    if ((distrib->d_name[1] == '.') && (distrib->d_name[2] == 0)) continue;	    	    
	  } 
	  
          sprintf(path, "%s/%d/layout_%u/spare_%u/%s", 
		storage_to_rebuild->root, device_it, layout_it, spare_it, distrib->d_name);

	  dir2 = opendir(path);
	  if (dir2 == NULL) continue;	

	  // Loop on file stored in distibution sub directory
	  while ((file = readdir(dir2)) != NULL) {
	    int i;

	    // Just care about file with .hdr suffix
            if (strstr(file->d_name, ".hdr") == NULL) continue;

            // Read the file
            sprintf(path, "%s/%d/layout_%u/spare_%u/%s/%s", 
		storage_to_rebuild->root, device_it, layout_it, spare_it, distrib->d_name,file->d_name);

	    fd = open(path, ROZOFS_ST_NO_CREATE_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
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
	        // Let's re-write the header file on the device to rebuild            
                sprintf(path, "%s/%d/layout_%u/spare_%u/%s/", 
		       storage_to_rebuild->root, device_to_rebuild, layout_it, spare_it, distrib->d_name); ;  
                storage_write_header_file(NULL,dev,path,&file_hdr);		   		
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
            file_entry.todo = 1;      
            memcpy(file_entry.dist_set_current, file_hdr.dist_set_current, sizeof (sid_t) * ROZOFS_SAFE_MAX);	    
	        
            ret = write(cfgfd[idx],&file_entry,sizeof(file_entry)); 
	    if (ret != sizeof(file_entry)) {
	      severe("can not write file cid%d sid%d %d %s",cid,sid,idx,strerror(errno));
	    }
	    
	    idx++;
	    if (idx >= parallel) idx = 0; 

	  } // End of loop on file in a distribution 
	  closedir(dir2);  
	} // End of loop on distributions
	closedir(dir1);
      }
    }
  } 

  for (idx=0; idx < parallel; idx++) {
    close(cfgfd[idx]);
  }  
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
      system(cmd);
      exit(0);
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
    if (status != 0) failure++;
    else             success++;
  }
  
  if (failure != 0) {
    severe("%d failures occured");
    return -1;
  }
  return 0;
}

int rbs_sanity_check(const char *export_host, cid_t cid, sid_t sid,
        const char *root) {

    int status = -1;

    DEBUG_FUNCTION;

    // Try to initialize the storage to rebuild
    if (rbs_initialize(cid, sid, root) != 0) {
        // Probably a path problem
        fprintf(stderr, "Can't initialize rebuild storage (cid:%u; sid:%u;"
                " path:%s): %s\n", cid, sid, root, strerror(errno));
        goto out;
    }
    
    // Try to get the list of storages for this cluster ID
    if (rbs_get_cluster_list(&rpcclt_export, export_host, cid,
            &cluster_entries) != 0) {
        fprintf(stderr, "Can't get list of others cluster members from export"
                " server (%s) for storage to rebuild (cid:%u; sid:%u): %s\n",
                export_host, cid, sid, strerror(errno));
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

int rbs_rebuild_storage(const char *export_host, cid_t cid, sid_t sid,
        const char *root, uint8_t stor_idx, int device,
	int parallel, char * config_file) {
    list_t *p, *q;
    uint64_t current_nb_rb_files = 0;
    int status = -1;
    int nb_files;
    int ret;

    DEBUG_FUNCTION;

    rb_hash_table_initialize();

    // Initialize the storage to rebuild
    if (rbs_initialize(cid, sid, root) != 0) {
        severe("can't init. storage to rebuild (cid:%u;sid:%u;path:%s)",
                cid, sid, root);
        goto out;
    }
    strcpy(st2rebuild.export_hostname,export_host);
    strcpy(st2rebuild.config_file,config_file);


    // Get the list of storages for this cluster ID
    if (rbs_get_cluster_list(&rpcclt_export, export_host, cid,
            &cluster_entries) != 0) {
        severe("rbs_get_cluster_list failed (cid: %u) : %s", cid, strerror(errno));
        goto out;
    }

    // Check the list of cluster
    if (rbs_check_cluster_list(&cluster_entries, cid, sid) != 0)
        goto out;

    // Get connections for this given cluster
    if (rbs_init_cluster_cnts(&cluster_entries, cid, sid) != 0)
        goto out;

    // Get the list of bins files to rebuild for this storage
    if (device == -1) {
      // Build the list from the remote storages
      if (rbs_get_rb_entry_list_one_cluster(&cluster_entries, cid, sid, parallel) != 0)
        goto out;  	 	 	 
    }
    else {
      // Build the list from the available data on local disk
      if (rbs_build_device_missing_list_one_cluster(&cluster_entries, cid, sid, device, parallel) != 0)
        goto out;	    		
    }
    
    rb_hash_table_delete();

    // Free cluster(s) list
    rbs_release_cluster_list(&cluster_entries);
    
    status = 0;


    // Actually process the rebuild
    info("%d files to rebuild by %d processes",rb_fid_table_count,parallel);
    ret = rbs_do_list_rebuild();
    while (ret != 0) {
      sleep(TIME_BETWEEN_2_RB_ATTEMPS);    
      ret = rbs_do_list_rebuild();
    }
    unlink(get_rebuild_directory_name());
    return status;
    
out:
    rb_hash_table_delete();

    // Free cluster(s) list
    rbs_release_cluster_list(&cluster_entries);
 
    return status;
}

