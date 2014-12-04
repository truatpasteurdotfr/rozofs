#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <sys/vfs.h>
 
#include <rozofs/rozofs_srv.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/transform.h>
#include <src/storaged/storage.h>
/*
**____________________________________________________
*/
/*
  Allocate a device for a file
  
   @param st: storage context
*/
uint32_t storio_device_mapping_allocate_device(storage_t * st) {
  struct statfs sfs;
  int           dev;
  uint64_t      max=0;
  int           choosen_dev=0;
  char          path[FILENAME_MAX];  
  
  for (dev = 0; dev < st->device_number; dev++) {

    sprintf(path, "%s/%d/", st->root, dev); 
               
    if (statfs(path, &sfs) != -1) {
      if (sfs.f_bfree > max) {
        max         = sfs.f_bfree;
	choosen_dev = dev;
      }
    }
  }  
  return choosen_dev;
}
int main(int argc, char **argv) {
    storage_t st;
    sid_t sid = 1;
    cid_t cid = 1;
    sstat_t sst;
    bin_t *bins_write_1;
    bin_t *bins_write_2;
    bin_t *bins_read_1;
    bin_t *bins_read_2;
    uint8_t layout;
    uint8_t dist_set[ROZOFS_SAFE_MAX];
    uint8_t spare;
    fid_t fid;
    bid_t bid;
    uint32_t nrb;
    tid_t tid_1;
    tid_t tid_2;
    uint64_t file_size;
    uint8_t write_version;
    uint8_t i = 0;
    uint8_t device_id[ROZOFS_STORAGE_MAX_CHUNK_PER_FILE];
    int     is_fid_faulty;
    uint32_t bsize = ROZOFS_BSIZE_4K;

    // Initialize the layout table
    rozofs_layout_initialize();

    // Initialize the storage root ditectory
    fprintf(stdout, "Initialize storage with SID: %u\n", sid);
    if (storage_initialize(&st, cid, sid, "/tmp",6,4,2,-1,NULL) != 0) {
        perror("failed to initialize storage");
        exit(-1);
    }

    // Stat the storage
    if (storage_stat(&st, &sst) != 0) {
        perror("failed to stat storage");
        exit(-1);
    }
    fprintf(stdout, "Stats for storage with SID: %u\n size: %" PRIu64 "\n free: %" PRIu64 "\n", sid, sst.size, sst.free);

    // Prepare parameters
    uuid_generate(fid);
    memset(&dist_set, 0, sizeof (sid_t) * ROZOFS_SAFE_MAX);
    spare = 0;
    bid = 0;
    nrb = 10;
    tid_1 = 0;
    tid_2 = 2;
    layout = 0;
    write_version = 0;

    // Write some bins (nrb. projections with projection = tid)
    bins_write_1 = xmalloc(nrb * rozofs_get_max_psize(layout,bsize) * sizeof (bin_t));
    memset(bins_write_1, 1, nrb * rozofs_get_max_psize(layout,bsize) * sizeof (bin_t));

    bins_write_2 = xmalloc(nrb * rozofs_get_max_psize(layout,bsize) * sizeof (bin_t));
    memset(bins_write_2, 2, nrb * rozofs_get_max_psize(layout,bsize) * sizeof (bin_t));

    bins_read_1 = xmalloc(nrb * rozofs_get_max_psize(layout,bsize) * sizeof (bin_t));
    bins_read_2 = xmalloc(nrb * rozofs_get_max_psize(layout,bsize) * sizeof (bin_t));

    // For each layout
    for (layout = 0; layout < LAYOUT_MAX; layout++) {

        // For spare and no spare
        for (i = 0; i < 2; i++) {

            spare = i;
            uuid_generate(fid);
            size_t len_read = 0;

            // Write projections
            fprintf(stdout, "----------------------------------------------\n");
            fprintf(stdout, "Write/Read tests for layout=%u and spare=%u\n", layout, spare);
            fprintf(stdout, "----------------------------------------------\n");

            bid = 0;

            // Write projections
            fprintf(stdout, "Write %u projections (id=%u and sizeof: %u bins) at bid=%"PRIu64"\n", nrb, tid_1, rozofs_get_psizes(layout,bsize, tid_1), bid);
	    
	    memset(device_id,ROZOFS_UNKNOWN_CHUNK,ROZOFS_SAFE_MAX);
            if (storage_write(&st, device_id, layout, bsize, (uint8_t *) & dist_set, spare, fid, bid, nrb, write_version, &file_size, bins_write_1, &is_fid_faulty) != 0) {
                perror("failed to write bins");
                exit(-1);
            }

            bid = bid + nrb;

            fprintf(stdout, "Write %u projections (id=%u and sizeof: %u bins) at bid=%"PRIu64"\n", nrb, tid_2, rozofs_get_psizes(layout, bsize,tid_2), bid);
            
            if (storage_write(&st,device_id, layout, bsize, (uint8_t *) & dist_set, spare, fid, bid, nrb, write_version, &file_size, bins_write_2, &is_fid_faulty) != 0) {
                perror("failed to write bins");
                exit(-1);
            }

            bid = bid - nrb;

            fprintf(stdout, "Read %u projections (id=%u and sizeof: %u bins) at bid=%"PRIu64"\n", nrb, tid_1, rozofs_get_psizes(layout, bsize,tid_1), bid);

            if (storage_read(&st, device_id, layout, bsize, (uint8_t *) & dist_set, spare, fid, bid, nrb, bins_read_1, &len_read, &file_size, &is_fid_faulty) != 0) {
                perror("failed to read bins");
                exit(-1);
            }

            if (memcmp(bins_write_1, bins_read_1, nrb * rozofs_get_psizes(layout,bsize, tid_1) * sizeof (bin_t)) != 0) {
                fprintf(stdout, " Compare projections FALSE for projections with id=%u\n", tid_1);
                exit(-1);
            } else {
                fprintf(stdout, " Compare projections OK for projections with id=%u\n", tid_1);
            }

            bid = bid + nrb;

            fprintf(stdout, "Read %u projections (id=%u and sizeof: %u bins) at bid=%"PRIu64"\n", nrb, tid_2, rozofs_get_psizes(layout, bsize,tid_2), bid);

            if (storage_read(&st, device_id, layout, bsize, (uint8_t *) & dist_set, spare, fid, bid, nrb, bins_read_2, &len_read, &file_size, &is_fid_faulty) != 0) {
                perror("failed to read bins");
                exit(-1);
            }

            if (memcmp(bins_write_2, bins_read_2, nrb * rozofs_get_psizes(layout,bsize, tid_2) * sizeof (bin_t)) != 0) {
                fprintf(stdout, " Compare projections FALSE for projections with id=%u\n", tid_2);
                exit(-1);
            } else {
                fprintf(stdout, " Compare projections OK for projections with id=%u\n", tid_2);
            }

            if (storage_rm_file(&st, fid) != 0) {
                perror("failed to remove file bins");
                exit(-1);
            }

        }
    }

    /*


        if (storage_truncate(&st, fid, 0, 10) != 0) {
            perror("failed to truncate pfile");
            exit(-1);
        }

        if (storage_rm_file(&st, fid) != 0) {
            perror("failed to remove pfile");
            exit(-1);
        }

        storage_release(&st);
     */

    exit(0);
}
