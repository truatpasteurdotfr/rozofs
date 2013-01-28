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

#define _XOPEN_SOURCE 500

#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>

#include "mdir.h"
#include "mdirent_vers2.h"
#include "dirent_journal.h"

/**
 *  structure for rescue at cache entry level for a given bucket index:
 **  notice that the first entry is reserved for root
 */
typedef struct _dirent_cache_repair_cache_entry_list_t {
    int coll_idx; /**< index of the collision file or              */
    int first_index; /**< assert to 1 if the hash_entry is the first elt of bucket_idx liist in that
     cache entry */
    mdirents_cache_entry_t *cache_entry_p; /**< pointer to a valid cache entry or NULL     */
    mdirents_hash_entry_t *hash_entry_last_p; /**< last hash entry in that dirent cache entry */

} dirent_cache_repair_cache_entry_list_t;

typedef struct _dirent_cache_repair_hash_entry_list_t {
    int local_idx; /**< local index within the dirent cache entry */
    mdirents_hash_entry_t *hash_entry_p; /**< pointer to the hash entry within the dirent cache entry */
} dirent_cache_repair_hash_entry_list_t;

/*
 **______________________________________________________________________________
 */
/**
 *   GLOBAL DATA
 */
/**
 * buffer to rebuild a linked list of dirent cache entries
 */
dirent_cache_repair_cache_entry_list_t dirent_cache_repair_cache_entry_list_tab[MDIRENTS_ENTRIES_COUNT
        + 1];
/**
 * buffer to rebuild a linked list of hash_entries within a dirent cache entry
 */
dirent_cache_repair_hash_entry_list_t dirent_cache_repair_hash_entry_list_tab[MDIRENTS_ENTRIES_COUNT
        + 1];

/**
 * statistics buffer
 */
uint64_t dirent_repair_file_stats[DIRENT_REPAIR_MAX];
uint64_t dirent_repair_file_count = 0;
int dirent_repair_print_enable = 0;

/*
 **______________________________________________________________________________
 */
/**
 *  API: clear dirent file repair statistics
 *
 * @retval none
 */
void dirent_file_repair_stats_clear() {
    int i;
    for (i = 0; i < DIRENT_REPAIR_MAX; i++)
        dirent_repair_file_stats[i] = 0;

}
/*
 **______________________________________________________________________________
 */
/**
 *  API: print out the dirent file repair statistics
 *
 * @retval none
 */
void dirent_file_repair_stats_print() {
    printf("DIRENT FILE REPAIR statistics\n");
    printf("file repair counter (cause Loop detection  ) %8.8llu\n",
            (unsigned long long int) dirent_repair_file_stats[DIRENT_REPAIR_LOOP]);
    printf("file repair counter (cause Free entry      ) %8.8llu\n",
            (unsigned long long int) dirent_repair_file_stats[DIRENT_REPAIR_FREE]);
    printf("file repair counter (cause No Memory       ) %8.8llu\n",
            (unsigned long long int) dirent_repair_file_stats[DIRENT_REPAIR_MEM]);
    printf("file repair counter (cause Bucket Mismatch ) %8.8llu\n",
            (unsigned long long int) dirent_repair_file_stats[DIRENT_REPAIR_BUCKET_IDX_MISMATCH]);
    printf("file repair counter (cause No Coll. File   ) %8.8llu\n",
            (unsigned long long int) dirent_repair_file_stats[DIRENT_REPAIR_NO_COLL_FILE]);
    printf("file repair counter (cause EOF missing     ) %8.8llu\n",
            (unsigned long long int) dirent_repair_file_stats[DIRENT_REPAIR_NO_EOF]);
    printf("Total Files repaired                         %8.8llu\n",
            (unsigned long long int) dirent_repair_file_count);

    printf("\n");

}

/*
 **______________________________________________________________________________
 */
/**
 *  Debug function: print the content of the hash entry rebuild buffer
 *
 * @retval  0 on success
 * @retval -1 on failure
 */

void dirent_cache_repair_cache_entry_for_bucket_idx_print(int bucket_idx,
        char *name) {
    int i;
    mdirents_hash_entry_t *hash_entry_p;
    if (dirent_repair_print_enable == 0)
        return; // FDL DEBUG

    printf("hash entry repair buffer for buck_idx %d %s\n", bucket_idx, name);
    for (i = 0; i < MDIRENTS_ENTRIES_COUNT; i++) {
        hash_entry_p = dirent_cache_repair_hash_entry_list_tab[i].hash_entry_p;
        if (hash_entry_p == NULL ) {
            /*
             ** end of list
             */
            break;
        }
        printf(" Local Idx :%3.3d [t:%d idx %3.3d]",
                dirent_cache_repair_hash_entry_list_tab[i].local_idx,
                hash_entry_p->next.type, hash_entry_p->next.idx);
        if (hash_entry_p->next.type != MDIRENTS_HASH_PTR_LOCAL) {
            printf(" <--*\n");

        } else
            printf("\n");
    }
}
/*
 **______________________________________________________________________________
 */
/**
 *  Debug function: print the content of the hash entry rebuild buffer
 *
 * @retval  0 on success
 * @retval -1 on failure
 */

void dirent_cache_repair_printf_cache_entry_for_bucket_idx(
        mdirents_cache_entry_t *cache_entry_p, int bucket_idx, int coll_idx) {
    mdirent_sector0_not_aligned_t *sect0_p;
    int hash_entry_idx = 0;
    int next_hash_entry_idx;
    int table_idx = 0;
    int hash_entry_bucket_idx;
    mdirents_hash_entry_t *hash_entry_p = NULL;
    mdirents_hash_ptr_t *hash_bucket_p;

    memset(dirent_cache_repair_hash_entry_list_tab, 0,
            sizeof(dirent_cache_repair_hash_entry_list_t)
                    * MDIRENTS_ENTRIES_COUNT);

    if (dirent_repair_print_enable == 0)
        return; // FDL DEBUG

    sect0_p = DIRENT_VIRT_TO_PHY_OFF(cache_entry_p,sect0_p)
    ;
    hash_bucket_p = DIRENT_CACHE_GET_BUCKET_PTR(cache_entry_p,bucket_idx)
    ;
    if (hash_bucket_p == NULL ) {
        /*
         ** That case Must not occur since we have elready scanned the entry
         ** One solution, if it happens is to skip that cache entry or to create one memory
         ** array for the range of bucket_idx.
         */
        printf("Invalid hash_bucket_p\n");
        return;
    }

    printf(" Coll Idx %3.3d Bucket Idx Entry :[t:%d idx %3.3d]", coll_idx,
            hash_bucket_p->type, hash_bucket_p->idx);

    /*
     ** go through the bitmap to find out the entry that belongs to the requested linked list
     */
    while (hash_entry_idx < MDIRENTS_ENTRIES_COUNT) {
        next_hash_entry_idx =
                DIRENT_CACHE_GETNEXT_ALLOCATED_HASH_ENTRY_IDX(&sect0_p->hash_bitmap,hash_entry_idx)
        ;
        if (next_hash_entry_idx < 0) {
            /*
             ** all the entry of that dirent cache entry have been scanned,
             */
            break;

        }
        hash_entry_idx = next_hash_entry_idx;
        /*
         ** need to get the hash entry context and then the pointer to the name entry. The hash entry context is
         ** needed since it contains the reference of the starting chunk of the name entry
         */
        hash_entry_p =
                (mdirents_hash_entry_t*) DIRENT_CACHE_GET_HASH_ENTRY_PTR(cache_entry_p,hash_entry_idx)
        ;
        if (hash_entry_p == NULL ) {
            /*
             ** something wrong!! (either the index is out of range and the memory array has been released
             */
            /*
             ** ok, let check the next hash_entry
             */
            hash_entry_idx++;
            continue;
        }
        hash_entry_bucket_idx = DIRENT_HASH_ENTRY_GET_BUCKET_IDX(hash_entry_p);
        if ((hash_entry_bucket_idx == bucket_idx)
                && (hash_entry_p->next.type != MDIRENTS_HASH_PTR_FREE)) {
            /*
             ** store it since it matches the searched linked list
             */
            dirent_cache_repair_hash_entry_list_tab[table_idx].hash_entry_p =
                    hash_entry_p;
            dirent_cache_repair_hash_entry_list_tab[table_idx].local_idx =
                    hash_entry_idx;
            table_idx++;
        }
        /*
         ** ok, let check the next hash_entry
         */
        hash_entry_idx++;
        continue;
    }
    dirent_cache_repair_cache_entry_for_bucket_idx_print(bucket_idx,
            "AFTER REPAIR");
    printf("\n\n");

}

/*
 **______________________________________________________________________________
 */
/**
 * Internal API to check the linked list of the hash entry within the linked list of the cache entry
 for a given bucket index.
 Each modified dirent cache entry will be re-written on disk during that procedure
 *
 * @param dir_fd: file handle of the parent directory
 * @param bucket_idx: bucket index for which the rebuild is requested
 * @param *root_entry_p: pointer to root dirent cache entry
 * @param cause: cause that triggers the dirent file repair
 * @param repair: assert to 1 to repair 0 to check
 *
 * @retval none
 */

void dirent_file_check(int dir_fd, mdirents_cache_entry_t *root_entry_p,
        int bucket_idx) {
    mdirents_cache_entry_t *cache_entry_p;
    //int hash_entry_idx = 0;
    //int cache_entry_tb_idx = 0;
    int coll_idx;
    int loop_cnt = 0;
    int next_coll_idx = 0;
    int bit_idx;
    int chunk_u8_idx;
    uint8_t *coll_bitmap_p;
    mdirent_sector0_not_aligned_t *sect0_p;

    /*
     ** set the different parameters
     */
    //cache_entry_tb_idx = 0;
    //hash_entry_idx = 0;
    coll_idx = 0;
    /*
     ** get the pointer to the collision file bitmap
     */
    sect0_p = DIRENT_VIRT_TO_PHY_OFF(root_entry_p,sect0_p)
    ;
    coll_bitmap_p = (uint8_t*) &sect0_p->coll_bitmap;
    /*
     ** case of the collision file, so need to go through the bitmap of the
     ** dirent root file
     */

    dirent_cache_repair_printf_cache_entry_for_bucket_idx(root_entry_p,
            bucket_idx, -1);
    cache_entry_p = NULL;
    while (coll_idx < MDIRENTS_MAX_COLLS_IDX) {
        chunk_u8_idx = coll_idx / 8;
        bit_idx = coll_idx % 8;
        /*
         ** there is no collision dirent entry or the collision dirent entry exist and is not full
         */
        if ((coll_bitmap_p[chunk_u8_idx] & (1 << bit_idx)) != 0) {
            /*
             ** That entry is free, need to find out the next entry that is busy (0: busy, 1:free)
             */
            if (coll_idx % 8 == 0) {
                next_coll_idx = check_bytes_val(coll_bitmap_p, coll_idx,
                        MDIRENTS_MAX_COLLS_IDX, &loop_cnt, 1);
                if (next_coll_idx < 0)
                    break;
                /*
                 ** next  chunk
                 */
                if (next_coll_idx == coll_idx)
                    coll_idx++;
                else
                    coll_idx = next_coll_idx;
                continue;
            }
            /*
             ** next chunk
             */
            //hash_entry_idx = 0;
            coll_idx++;
            continue;
        }
        /*
         ** one collision idx has been found
         ** need to get the entry associated with the collision index
         */
        cache_entry_p = dirent_cache_get_collision_ptr(root_entry_p, coll_idx);
        if (cache_entry_p == NULL ) {
            /*
             ** something is rotten in the cache since the pointer to the collision dirent cache
             ** does not exist
             */
//        DIRENT_SEVERE("dirent_file_repair no collision file for index %d error at %d\n",coll_idx,__LINE__);
            /*
             ** OK, do not break the analysis, skip that collision entry and try the next if any
             */
            coll_idx++;
            continue;
        }
        dirent_cache_repair_printf_cache_entry_for_bucket_idx(cache_entry_p,
                bucket_idx, coll_idx);
        coll_idx++;

    }

}

/*
 **______________________________________________________________________________
 */
/**
 * Internal API to rebuild the linked list of the hash entry within the linked list of the cache entry
 for a given bucket index.
 Each modified dirent cache entry will be re-written on disk during that procedure
 *
 * @param dir_fd: file handle of the parent directory
 * @param bucket_idx: bucket index for which the rebuild is requested
 * @param *root_entry_p: pointer to root dirent cache entry
 * @param cause: cause that triggers the dirent file repair
 * @param repair: assert to 1 to repair 0 to check
 *
 * @retval none
 */

void dirent_file_repair(int dir_fd, mdirents_cache_entry_t *root_entry_p,
        int bucket_idx, dirent_file_repair_cause_e cause) {
    mdirents_cache_entry_t *cache_entry_p;
    mdirents_cache_entry_t *cache_entry_p_next;
    mdirents_hash_entry_t *hash_entry_p = NULL;
    //int hash_entry_idx = 0;
    int cache_entry_tb_idx = 0;
    int coll_idx;
    int loop_cnt = 0;
    int next_coll_idx = 0;
    int first_index;
    int bit_idx;
    int chunk_u8_idx;
    uint8_t *coll_bitmap_p;
    mdirents_hash_ptr_t *hash_bucket_p;
    int i;
    mdirent_sector0_not_aligned_t *sect0_p;

    dirent_repair_file_stats[cause]++;

    memset(dirent_cache_repair_cache_entry_list_tab, 0,
            sizeof(dirent_cache_repair_cache_entry_list_t)
                    * MDIRENTS_ENTRIES_COUNT);

    /*
     ** set the different parameters
     */
    cache_entry_tb_idx = 0;
    //hash_entry_idx = 0;
    coll_idx = 0;
    /*
     ** get the pointer to the collision file bitmap
     */
    sect0_p = DIRENT_VIRT_TO_PHY_OFF(root_entry_p,sect0_p)
    ;
    coll_bitmap_p = (uint8_t*) &sect0_p->coll_bitmap;

    /*
     ** First of all do the recovery for the root entry
     */
    dirent_cache_repair_cache_entry_for_bucket_idx(dir_fd, root_entry_p,
            root_entry_p, bucket_idx, &hash_entry_p, &first_index);
    /*
     ** insert the result in the rescue table
     */
    dirent_cache_repair_cache_entry_list_tab[cache_entry_tb_idx].coll_idx = -1; // indicate root
    dirent_cache_repair_cache_entry_list_tab[cache_entry_tb_idx].first_index =
            first_index;
    dirent_cache_repair_cache_entry_list_tab[cache_entry_tb_idx].cache_entry_p =
            root_entry_p;
    dirent_cache_repair_cache_entry_list_tab[cache_entry_tb_idx].hash_entry_last_p =
            hash_entry_p;
    cache_entry_tb_idx++;

    /*
     ** case of the collision file, so need to go through the bitmap of the
     ** dirent root file
     */
    cache_entry_p = NULL;
    while (coll_idx < MDIRENTS_MAX_COLLS_IDX) {
        chunk_u8_idx = coll_idx / 8;
        bit_idx = coll_idx % 8;
        /*
         ** there is no collision dirent entry or the collision dirent entry exist and is not full
         */
        if ((coll_bitmap_p[chunk_u8_idx] & (1 << bit_idx)) != 0) {
            /*
             ** That entry is free, need to find out the next entry that is busy (0: busy, 1:free)
             */
            if (coll_idx % 8 == 0) {
                next_coll_idx = check_bytes_val(coll_bitmap_p, coll_idx,
                        MDIRENTS_MAX_COLLS_IDX, &loop_cnt, 1);
                if (next_coll_idx < 0)
                    break;
                /*
                 ** next  chunk
                 */
                if (next_coll_idx == coll_idx)
                    coll_idx++;
                else
                    coll_idx = next_coll_idx;
                continue;
            }
            /*
             ** next chunk
             */
            //hash_entry_idx = 0;
            coll_idx++;
            continue;
        }
        /*
         ** one collision idx has been found
         ** need to get the entry associated with the collision index
         */
        cache_entry_p = dirent_cache_get_collision_ptr(root_entry_p, coll_idx);
        if (cache_entry_p == NULL ) {
            /*
             ** something is rotten in the cache since the pointer to the collision dirent cache
             ** does not exist
             */
//        DIRENT_SEVERE("dirent_file_repair no collision file for index %d error at %d\n",coll_idx,__LINE__);
            /*
             ** OK, do not break the analysis, skip that collision entry and try the next if any
             */
            coll_idx++;
            continue;
        }
        /*
         ** OK, let's try to repair that current dirent cache entry for that bucket idx
         */
        dirent_cache_repair_cache_entry_for_bucket_idx(dir_fd, root_entry_p,
                cache_entry_p, bucket_idx, &hash_entry_p, &first_index);
        /*
         ** insert the result in the rescue table if there is at least one valid entry for that bucket idx
         */
        if (hash_entry_p != NULL ) {
            dirent_cache_repair_cache_entry_list_tab[cache_entry_tb_idx].coll_idx =
                    coll_idx;
            dirent_cache_repair_cache_entry_list_tab[cache_entry_tb_idx].first_index =
                    first_index;
            dirent_cache_repair_cache_entry_list_tab[cache_entry_tb_idx].cache_entry_p =
                    cache_entry_p;
            dirent_cache_repair_cache_entry_list_tab[cache_entry_tb_idx].hash_entry_last_p =
                    hash_entry_p;
            cache_entry_tb_idx++;

        }
        coll_idx++;

    }
    /*
     ** OK now go through that table to find out the order of the cache entries to update each of the
     ** local hash entries
     */
    for (i = 0; i < cache_entry_tb_idx; i++) {
        cache_entry_p =
                dirent_cache_repair_cache_entry_list_tab[i].cache_entry_p;
        hash_entry_p =
                dirent_cache_repair_cache_entry_list_tab[i].hash_entry_last_p;
        if (hash_entry_p == NULL ) {
            if (i == 0) {
                /*
                 ** get the pointer to hash bucket table
                 */
                hash_bucket_p =
                        DIRENT_CACHE_GET_BUCKET_PTR(cache_entry_p,bucket_idx)
                ;
                if (hash_bucket_p == NULL ) {
                    /*
                     ** That case Must not occur since we have elready scanned the entry
                     ** One solution, if it happens is to skip that cache entry or to create one memory
                     ** array for the range of bucket_idx.
                     */
                    DIRENT_SEVERE("dirent_file_repair: hash_bucket_p is null for bucket_idx %d (line %d)\n",bucket_idx,__LINE__)
                    ;
                    exit(0);
                }
                /*
                 ** check the end of list case
                 */
                cache_entry_p_next = dirent_cache_repair_cache_entry_list_tab[i
                        + 1].cache_entry_p;
                if (cache_entry_p_next == NULL ) {
                    /*
                     ** end of list case
                     */
                    break;
                }
                hash_bucket_p->type = MDIRENTS_HASH_PTR_COLL;
                hash_bucket_p->idx = dirent_cache_repair_cache_entry_list_tab[i
                        + 1].coll_idx;
                dirent_cache_repair_printf_cache_entry_for_bucket_idx(
                        cache_entry_p, bucket_idx, -1);
                write_mdirents_file(dir_fd, cache_entry_p);

                continue;
            }
            DIRENT_SEVERE("dirent_file_repair: hash_bucket_p is null for bucket_idx %d (line %d)\n",bucket_idx,__LINE__)
            ;
            exit(0);

        }
        /*
         ** Check if there is a cache entry in the next entry of the rescue table
         */
        cache_entry_p_next =
                dirent_cache_repair_cache_entry_list_tab[i + 1].cache_entry_p;
        if (cache_entry_p_next == NULL ) {
            /*
             ** end of list case
             */
            break;
        }
        /*
         ** there is an entry so update our current last hash_entry and eventually our bucket entry
         ** index according to the value of first_index
         */
        hash_entry_p->next.type = MDIRENTS_HASH_PTR_COLL;
        hash_entry_p->next.idx =
                dirent_cache_repair_cache_entry_list_tab[i + 1].coll_idx;
        dirent_cache_repair_printf_cache_entry_for_bucket_idx(cache_entry_p,
                bucket_idx,
                dirent_cache_repair_cache_entry_list_tab[i].coll_idx);
        write_mdirents_file(dir_fd, cache_entry_p);

#if 0 // useless since already done per hash_entry repair->
        if (dirent_cache_repair_cache_entry_list_tab[i].first_index)
        {
            /*
             ** get the pointer to hash bucket table
             */
            hash_bucket_p = DIRENT_CACHE_GET_BUCKET_PTR(cache_entry_p,bucket_idx);
            if (hash_bucket_p == NULL)
            {
                /*
                 ** That case Must not occur since we have elready scanned the entry
                 ** One solution, if it happens is to skip that cache entry or to create one memory
                 ** array for the range of bucket_idx.
                 */
                DIRENT_SEVERE("dirent_file_repair: hash_bucket_p is null for bucket_idx %d (line %d)\n",bucket_idx,__LINE__);
                exit(0);
            }
            hash_bucket_p->type = MDIRENTS_HASH_PTR_LOCAL;
            hash_bucket_p->idx = dirent_cache_repair_cache_entry_list_tab[i+1].coll_idx;
        }
#endif
    }
    /*
     ** we have the end of list so now, update the last one
     */
    if (cache_entry_p == NULL ) {
        /*
         ** We must have at least the root !!
         */
        DIRENT_SEVERE("dirent_file_repair: empty list for bucket %d (line %d)\n",bucket_idx,__LINE__)
        ;
        exit(0);
    }
    if (hash_entry_p == NULL ) {
        /*
         ** this is possible for the case of the root only
         */
        if (cache_entry_p != root_entry_p) {
            DIRENT_SEVERE("dirent_file_repair: collision entry empty for bucket %d (line %d)\n",bucket_idx,__LINE__)
            ;
            exit(0);
        }
        hash_bucket_p = DIRENT_CACHE_GET_BUCKET_PTR(cache_entry_p,bucket_idx)
        ;
        if (hash_bucket_p == NULL ) {
            /*
             ** That case Must not occur since we have elready scanned the entry
             ** One solution, if it happens is to skip that cache entry or to create one memory
             ** array for the range of bucket_idx.
             */
            DIRENT_SEVERE("dirent_file_repair: hash_bucket_p is null for bucket_idx %d (line %d)\n",bucket_idx,__LINE__)
            ;
            exit(0);
        }
        hash_bucket_p->type = MDIRENTS_HASH_PTR_EOF;
        hash_bucket_p->idx = 0;
        write_mdirents_file(dir_fd, cache_entry_p);

    } else {
        hash_entry_p->next.type = MDIRENTS_HASH_PTR_EOF;
        hash_entry_p->next.idx = 0;
    }
    write_mdirents_file(dir_fd, root_entry_p);

    dirent_cache_repair_printf_cache_entry_for_bucket_idx(cache_entry_p,
            bucket_idx, dirent_cache_repair_cache_entry_list_tab[i].coll_idx);
//   exit(0);

}

/*
 **______________________________________________________________________________
 */
/**
 * API to repair a faulty dirent file (collision or root)
 *
 * @param fd_dir: file handle of the parent directory
 * @param root: pointer to the mdirent root entry
 * @param cache_entry_p: pointer to the mdirent entry to repair
 * @param bucket_idx : bucket idx for which the issue has been detected
 *
 * @retval  0 on success
 * @retval -1 on failure
 */
void dirent_cache_repair_cache_entry_for_bucket_idx(int fd_dir,
        mdirents_cache_entry_t *root, mdirents_cache_entry_t *cache_entry_p,
        int bucket_idx, mdirents_hash_entry_t **hash_entry_last_p,
        int *first_index) {
    mdirent_sector0_not_aligned_t *sect0_p;
    int hash_entry_idx = 0;
    int next_hash_entry_idx;
    int table_idx = 0;
    int hash_entry_bucket_idx;
    mdirents_hash_entry_t *hash_entry_p = NULL;
    mdirents_hash_ptr_t *hash_bucket_p;

    /*
     ** clear the returned parameters first
     */
    *hash_entry_last_p = NULL;
    *first_index = 0;

    /*
     ** clean up the tracking buffer
     */
    memset(dirent_cache_repair_hash_entry_list_tab, 0,
            sizeof(dirent_cache_repair_hash_entry_list_t)
                    * MDIRENTS_ENTRIES_COUNT);

    sect0_p = DIRENT_VIRT_TO_PHY_OFF(cache_entry_p,sect0_p)
    ;
    if (sect0_p == (mdirent_sector0_not_aligned_t*) NULL ) {
        DIRENT_SEVERE("dirent_cache_repair_cache_entry sector 0 ptr does not exist( line %d\n)",__LINE__)
        ;
        return;
    }
    /*
     ** go through the bitmap to find out the entry that belongs to the requested linked list
     */
    while (hash_entry_idx < MDIRENTS_ENTRIES_COUNT) {
        next_hash_entry_idx =
                DIRENT_CACHE_GETNEXT_ALLOCATED_HASH_ENTRY_IDX(&sect0_p->hash_bitmap,hash_entry_idx)
        ;
        if (next_hash_entry_idx < 0) {
            /*
             ** all the entry of that dirent cache entry have been scanned,
             */
            break;

        }
        hash_entry_idx = next_hash_entry_idx;
        /*
         ** need to get the hash entry context and then the pointer to the name entry. The hash entry context is
         ** needed since it contains the reference of the starting chunk of the name entry
         */
        hash_entry_p =
                (mdirents_hash_entry_t*) DIRENT_CACHE_GET_HASH_ENTRY_PTR(cache_entry_p,hash_entry_idx)
        ;
        if (hash_entry_p == NULL ) {
            /*
             ** something wrong!! (either the index is out of range and the memory array has been released
             */
            DIRENT_SEVERE("list_mdirentries pointer does not exist at %d\n",__LINE__)
            ;
            /*
             ** ok, let check the next hash_entry
             */
            hash_entry_idx++;
            continue;
        }
//     if (hash_entry_idx == 572) printf("hash_entry_bucket_idx%d\n",(int)DIRENT_HASH_ENTRY_GET_BUCKET_IDX(hash_entry_p));
        hash_entry_bucket_idx = DIRENT_HASH_ENTRY_GET_BUCKET_IDX(hash_entry_p);
        if ((hash_entry_bucket_idx == bucket_idx)
                && (hash_entry_p->next.type != MDIRENTS_HASH_PTR_FREE)) {
            /*
             ** store it since it matches the searched linked list
             */
            dirent_cache_repair_hash_entry_list_tab[table_idx].hash_entry_p =
                    hash_entry_p;
            dirent_cache_repair_hash_entry_list_tab[table_idx].local_idx =
                    hash_entry_idx;
            table_idx++;
        }
        /*
         ** ok, let check the next hash_entry
         */
        hash_entry_idx++;
        continue;
    }
    dirent_cache_repair_cache_entry_for_bucket_idx_print(bucket_idx, "BEFORE");
    /*
     ** OK, now repair the current dirent cache entry in memory
     */
    {

        int i;
        mdirents_hash_entry_t *hash_entry_p_next = NULL;
        int hash_entry_end_found = 0;
        mdirents_hash_entry_t hash_entry_end;
        int type;
        int eof = 0;
        hash_entry_end.next.type = 0;
        hash_entry_end.next.idx = 0;
        hash_entry_end.next.bucket_idx_low = 0;

        dirent_repair_file_count += table_idx;

        for (i = 0; i < MDIRENTS_ENTRIES_COUNT; i++) {
            hash_entry_p =
                    dirent_cache_repair_hash_entry_list_tab[i].hash_entry_p;
            if (hash_entry_p == NULL ) {
                /*
                 ** end of list
                 */
                break;
            }
            if (i == 0) {
                /*
                 ** update the hash bucket entry
                 */
                hash_bucket_p =
                        DIRENT_CACHE_GET_BUCKET_PTR(cache_entry_p,bucket_idx)
                ;
                if (hash_bucket_p == NULL ) {
                    /*
                     ** That case Must not occur since we have elready scanned the entry
                     ** One solution, if it happens is to skip that cache entry or to create one memory
                     ** array for the range of bucket_idx.
                     */
                    DIRENT_SEVERE("dirent_cache_repair_cache_entry_for_bucket_idx: hash_bucket_p is null for bucket_idx %d (line %d)\n",bucket_idx,__LINE__)
                    ;
                    return;
                }
                hash_bucket_p->type = MDIRENTS_HASH_PTR_LOCAL;
                hash_bucket_p->idx =
                        dirent_cache_repair_hash_entry_list_tab[i].local_idx;
            }
            /*
             ** Update the next pointer
             */
            type = hash_entry_p->next.type;
            switch (type) {

            case MDIRENTS_HASH_PTR_COLL:
            case MDIRENTS_HASH_PTR_EOF:
                if (hash_entry_end_found == 0) {
                    hash_entry_end.next = hash_entry_p->next;
                    hash_entry_end_found = 1;
                }
                break;
            case MDIRENTS_HASH_PTR_LOCAL:
                /*
                 ** Check the next entry
                 */

                hash_entry_p_next = dirent_cache_repair_hash_entry_list_tab[i
                        + 1].hash_entry_p;
                if (hash_entry_p_next == NULL ) {
                    /*
                     ** end of list case
                     */
                    eof = 1;
                    break;
                }
                hash_entry_p->next.idx =
                        dirent_cache_repair_hash_entry_list_tab[i + 1].local_idx;
                hash_entry_p->next.type = MDIRENTS_HASH_PTR_LOCAL;
                break;

            default:
                DIRENT_SEVERE("bad type\n")
                ;
                exit(0);
            }
            if (eof)
                break;
        }
#if 1
        /*
         ** update the end of list
         */
        if (hash_entry_end_found == 1) {
            hash_entry_p->next = hash_entry_end.next;
        }
#endif
//       if (hash_entry_end_found == 0) printf("neither COLL nor EOF!!\n");
        *hash_entry_last_p = hash_entry_p;
    }
    /*
     ** check if there was no entry to be sure that the beginning of the local head of list is EOF
     ** this is applicable for an entry that is not root only
     */
    if (cache_entry_p != root) {
        if (table_idx == 0) {
            /*
             ** check if the current bucket is EOF, if not set it and re-write the file
             */
            hash_bucket_p =
                    DIRENT_CACHE_GET_BUCKET_PTR(cache_entry_p,bucket_idx)
            ;
            if ((hash_bucket_p != NULL )&&(hash_bucket_p->type != MDIRENTS_HASH_PTR_EOF)){
            hash_bucket_p->type =MDIRENTS_HASH_PTR_EOF;
            hash_bucket_p->idx =0;
            write_mdirents_file(fd_dir,cache_entry_p);
        }
    }
}

    dirent_cache_repair_cache_entry_for_bucket_idx_print(bucket_idx, "AFTER");
}
