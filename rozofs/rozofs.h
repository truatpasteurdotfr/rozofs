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

#ifndef _ROZOFS_H
#define _ROZOFS_H

#include <stdint.h>
#include <uuid/uuid.h>

#include <config.h>

#define ROZOFS_UUID_SIZE 16
#define ROZOFS_HOSTNAME_MAX 128
#define ROZOFS_BSIZE 8192       // could it be export specific ?
#define ROZOFS_SAFE_MAX 36
#define ROZOFS_DIR_SIZE 4096
#define ROZOFS_PATH_MAX 1024
#define ROZOFS_XATTR_NAME_MAX 255
#define ROZOFS_XATTR_VALUE_MAX 65536
#define ROZOFS_XATTR_LIST_MAX 65536
#define ROZOFS_FILENAME_MAX 255

/* Value max for a SID */
#define SID_MAX 255
/* Value min for a SID */
#define SID_MIN 1
/* Nb. max of storage node for one volume */
#define STORAGE_NODES_MAX 64
/* Nb. max of storaged ports on the same storage node */
#define STORAGE_NODE_PORTS_MAX 32
/* Nb. max of storages (couple cid:sid) on the same storage node */
#define STORAGES_MAX_BY_STORAGE_NODE 32
/* First TCP port used */
#define STORAGE_PORT_NUM_BEGIN 40000

#define MAX_DIR_ENTRIES 100
#define ROZOFS_MD5_SIZE 22
#define ROZOFS_MD5_NONE "0000000000000000000000"

#define EPROTO_PROGRAM_CHECK    0x20000001
#define SPROTO_PROGRAM_CHECK    0x20000002
#define MPROTO_PROGRAM_CHECK    0x20000003
#define SMPROTO_PROGRAM_CHECK   0x20000004
#define SIMPROTO_PROGRAM_CHECK  0x20000005
#define RFMMPROTO_PROGRAM_CHECK 0x20000006

#define ROZOFS_EPROTO_TIMEOUT_SEC       10
#define ROZOFS_SPROTO_TIMEOUT_SEC       2
#define ROZOFS_MPROTO_TIMEOUT_SEC       2
#define ROZOFS_SMPROTO_TIMEOUT_SEC      2
#define ROZOFS_SIMPROTO_TIMEOUT_SEC     2
#define ROZOFS_RFMMPROTO_TIMEOUT_SEC    2

typedef enum {
    LAYOUT_2_3_4, LAYOUT_4_6_8, LAYOUT_8_12_16
} rozofs_layout_t;

typedef uint8_t tid_t;          /**< projection id */
typedef uint64_t bid_t;         /**< block id */
typedef uuid_t fid_t;           /**< file id */
typedef uint8_t sid_t;         /**< storage id */
typedef uint16_t cid_t;         /**< cluster id */
typedef uint16_t vid_t;         /**< volume id */
typedef uint32_t eid_t;         /**< export id */

// storage stat

typedef struct sstat {
    uint64_t size;
    uint64_t free;
} sstat_t;

typedef struct estat {
    uint16_t bsize;
    uint64_t blocks;
    uint64_t bfree;
    uint64_t files;
    uint64_t ffree;
    uint16_t namemax;
} estat_t;

typedef struct child {
    char *name;
    fid_t fid;
    struct child *next;
} child_t;

#include "common/transform.h"
//extern uint8_t rozofs_safe;
//extern uint8_t rozofs_forward;
//extern uint8_t rozofs_inverse;
//extern angle_t *rozofs_angles;
//extern uint16_t *rozofs_psizes;
//
//int rozofs_initialize(rozofs_layout_t layout);
//
//void rozofs_release();

#endif
