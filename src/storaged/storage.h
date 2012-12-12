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

#ifndef _STORAGE_H
#define _STORAGE_H

#include <stdint.h>
#include <limits.h>
#include <uuid/uuid.h>
#include <sys/param.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/list.h>
#include <rozofs/common/htable.h>

/** Maximum size in bytes for the header of file bins */
#define ROZOFS_ST_BINS_FILE_HDR_SIZE 5242880

/** Default open flags to use for open bins files */
#define ROZOFS_ST_BINS_FILE_FLAG O_RDWR | O_CREAT | O_NOATIME

/** Default mode to use for open bins files */
#define ROZOFS_ST_BINS_FILE_MODE S_IFREG | S_IRUSR | S_IWUSR

/** Default mode to use for create subdirectories */
#define ROZOFS_ST_DIR_MODE S_IRUSR | S_IWUSR | S_IXUSR

/** Directory used to store bins files for a specific storage ID*/
typedef struct storage {
    sid_t sid; ///< unique id of this storage.
    char root[FILENAME_MAX]; ///< absolute path.
} storage_t;

/**
 *  Header structure for one projection
 */
typedef union {
    uint64_t u64[2];

    struct {
        uint64_t timestamp : 64; ///<  time stamp. (not used yet)
        uint64_t effective_length : 16; ///<  effective length of the rebuilt block size: MAX is 64K.
        uint64_t projection_id : 8; ///<  index of the projection -> needed to find out angles/sizes: MAX is 255.
        uint64_t version : 8; ///<  version of rozofs. (not used yet)
        uint64_t filler : 32; ///<  for future usage.
    } s;
} rozofs_stor_bins_hdr_t;

/**
 *  Header structure for one file bins
 */
typedef struct rozofs_stor_bins_file_hdr {
    uint8_t layout; ///< layout used for this file.
    sid_t dist_set_current[ROZOFS_SAFE_MAX]; ///< currents sids of storage nodes target for this file.
    sid_t dist_set_next[ROZOFS_SAFE_MAX]; ///< next sids of storage nodes target for this. file (not used yet)
    uint8_t version; ///<  version of rozofs. (not used yet)
} rozofs_stor_bins_file_hdr_t;

/** Initialize a storage
 *
 * @param st: the storage to be initialized.
 * @param sid: the unique id for this storage.
 * @param root: the absolute path.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_initialize(storage_t *st, sid_t sid, const char *root);

/** Release a storage
 *
 * @param st: the storage to be released.
 */
void storage_release(storage_t * st);

/** Write nb_proj projections
 *
 * @param st: the storage to use.
 * @param layout: layout used for store this file.
 * @param dist_set: storages nodes used for store this file.
 * @param spare: indicator on the status of the projection.
 * @param fid: unique file id.
 * @param bid: first block idx (offset).
 * @param nb_proj: nb of projections to write.
 * @param proj_id: the projection id.
 * @param ts: time stamp. (not used yet)
 * @param effective_length: length of the last user block used for these proj.
 * @param version: version of rozofs used by the client. (not used yet)
 * @param *bins: bins to store.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_write(storage_t * st, uint8_t layout, sid_t * dist_set,
        uint8_t spare, fid_t fid, bid_t bid, uint32_t nb_proj,
        tid_t proj_id, uint64_t ts, uint16_t effective_length, uint8_t version,
        const bin_t * bins);

/** Read nb_proj projections
 *
 * @param st: the storage to use.
 * @param layout: layout used by this file.
 * @param dist_set: storages nodes used for store this file.
 * @param spare: indicator on the status of the projection.
 * @param fid: unique file id.
 * @param bid: first block idx (offset).
 * @param nb_proj: nb of projections to read.
 * @param proj_id: the projection id.
 * @param ts: time stamp. (not used yet)
 * @param effective_length: length of the last user block used for these proj.
 * @param version: version of rozofs used by the client. (not used yet)
 * @param *bins: bins to store.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_read(storage_t * st, uint8_t layout, sid_t * dist_set,
        uint8_t spare, fid_t fid, tid_t proj_id, bid_t bid, uint32_t nb_proj,
        uint64_t * ts, uint16_t * effective_length, uint8_t * version,
        bin_t * bins);

/** Truncate a bins file (not used yet)
 *
 * @param st: the storage to use.
 * @param layout: layout used by this file.
 * @param dist_set: storages nodes used for store this file.
 * @param spare: indicator on the status of the projection.
 * @param fid: unique file id.
 * @param proj_id: the projection id.
 * @param bid: first block idx (offset).
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_truncate(storage_t * st, uint8_t layout, sid_t * dist_set,
        uint8_t spare, fid_t fid, tid_t proj_id, bid_t bid);

/** Remove a bins file
 *
 * @param st: the storage to use.
 * @param layout: layout used by this file.
 * @param dist_set: storages nodes used for store this file.
 * @param fid: unique file id.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_rm_file(storage_t * st, uint8_t layout, sid_t * dist_set,
        fid_t fid);

/** Stat a storage
 *
 * @param st: the storage to use.
 * @param sstat: structure to use for store stats about this storage.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_stat(storage_t * st, sstat_t * sstat);

#endif