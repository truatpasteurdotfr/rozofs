/*
  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
  This file is part of Rozofs.

  Rozofs is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  Rozofs is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

%#include "rozofs.h"

/*
 * Common types
 */
typedef unsigned char   ep_uuid_t[ROZOFS_UUID_SIZE];
typedef string          ep_name_t<ROZOFS_FILENAME_MAX>;
typedef string          ep_path_t<ROZOFS_PATH_MAX>;
typedef string          ep_link_t<ROZOFS_PATH_MAX>;
typedef char            ep_host_t[ROZOFS_HOSTNAME_MAX];
typedef char            ep_md5_t[ROZOFS_MD5_SIZE];
 
enum ep_status_t {
    EP_SUCCESS = 0,
    EP_FAILURE = 1
};

union ep_status_ret_t switch (ep_status_t status) {
    case EP_FAILURE:    int error;
    default:            void;
};

struct ep_storage_t {
    uint16_t        sid;
    ep_host_t       host;
};

struct ep_cluster_t {
    uint16_t        cid;
    uint8_t         storages_nb;
    ep_storage_t    storages[ROZOFS_STORAGES_MAX];
};

struct ep_volume_t {
    uint8_t         clusters_nb;
    ep_cluster_t    clusters[ROZOFS_CLUSTERS_MAX];
};

struct ep_export_t {
    uint32_t        eid;
    ep_md5_t        md5;
    ep_uuid_t       rfid;   /*root fid*/
    int             rl;     /* rozofs layout */
    ep_volume_t     volume;
};

union ep_mount_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    ep_export_t export;
    case EP_FAILURE:    int         error;
    default:            void;
};

struct ep_mattr_t {
    ep_uuid_t   fid;
    uint16_t    cid;
    uint16_t    sids[ROZOFS_SAFE_MAX];
    uint32_t    mode;
    uint32_t    uid;
    uint32_t    gid;
    uint16_t    nlink;
    uint64_t    ctime;
    uint64_t    atime;
    uint64_t    mtime;
    uint64_t    size;
};

union ep_mattr_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    ep_mattr_t  attrs;
    case EP_FAILURE:    int         error;
    default:            void;
};


union ep_fid_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    ep_uuid_t   fid;
    case EP_FAILURE:    int         error;
    default:            void;
};

struct ep_lookup_arg_t {
    uint32_t    eid;
    ep_uuid_t   parent;
    ep_name_t   name;
};

struct ep_mfile_arg_t {
    uint32_t    eid;
    ep_uuid_t   fid;
};

struct ep_unlink_arg_t {
    uint32_t    eid;
    ep_uuid_t   pfid;
    ep_name_t   name;
};

struct ep_rmdir_arg_t {
    uint32_t    eid;
    ep_uuid_t   pfid;
    ep_name_t   name;
};

struct ep_statfs_t {
    uint16_t bsize;
    uint64_t blocks;
    uint64_t bfree;
    uint64_t files;
    uint64_t ffree;
    uint16_t namemax;
};

union ep_statfs_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    ep_statfs_t stat;
    case EP_FAILURE:    int         error;
    default:            void;
};

struct ep_setattr_arg_t {
    uint32_t    eid;
    uint32_t    to_set;
    ep_mattr_t  attrs;
};

union ep_getattr_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    ep_mattr_t  attrs;
    case EP_FAILURE:    int         error;
    default:            void;
};

union ep_readlink_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    ep_link_t   link;
    case EP_FAILURE:    int         error;
    default:            void;
};

struct ep_mknod_arg_t {
    uint32_t    eid;
    ep_uuid_t   parent;
    ep_name_t   name;
    uint32_t    uid;
    uint32_t    gid;
    uint32_t    mode;
};

struct ep_link_arg_t {
    uint32_t    eid;
    ep_uuid_t   inode;
    ep_uuid_t   newparent;
    ep_name_t   newname;
};

struct ep_mkdir_arg_t {
    uint32_t    eid;
    ep_uuid_t   parent;
    ep_name_t   name;
    uint32_t    uid;
    uint32_t    gid;
    uint32_t    mode;
};

struct ep_symlink_arg_t {
    uint32_t    eid;
    ep_link_t   link;
    ep_uuid_t   parent;
    ep_name_t   name;
};

typedef struct ep_child_t *ep_children_t;

struct ep_child_t {
    ep_name_t       name;
    ep_uuid_t       fid;
    ep_children_t   next;
};

struct dirlist_t {
	ep_children_t children;
	uint8_t eof;
        uint64_t cookie;
};

struct ep_readdir_arg_t {
    uint32_t    eid;
    ep_uuid_t   fid;
    uint64_t    cookie;
};

union ep_readdir_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    dirlist_t       reply;
    case EP_FAILURE:    int             error;
    default:            void;
};

struct ep_rename_arg_t {
    uint32_t    eid;
    ep_uuid_t   pfid;
    ep_name_t   name;
    ep_uuid_t   npfid;
    ep_name_t   newname;
};

struct ep_io_arg_t {
    uint32_t    eid;
    ep_uuid_t   fid;
    uint64_t    offset;
    uint32_t    length;
};

struct ep_write_block_arg_t {
    uint32_t    eid;
    ep_uuid_t   fid;
    uint64_t    bid;
    uint32_t    nrb;
    uint16_t    dist;
    uint64_t    offset;
    uint32_t    length;
};

struct ep_read_t {
    uint16_t    dist<>;
    int64_t     length;
};

union ep_read_block_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    ep_read_t    ret;
    case EP_FAILURE:    int         error;
    default:            void;
};

union ep_io_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    int64_t     length;
    case EP_FAILURE:    int         error;
    default:            void;
};

program EXPORT_PROGRAM {
    version EXPORT_VERSION {

        void
        EP_NULL(void)                           = 0;

        ep_mount_ret_t
        EP_MOUNT(ep_path_t)                     = 1;

        ep_status_ret_t
        EP_UMOUNT(uint32_t)                     = 2;

        ep_statfs_ret_t
        EP_STATFS(uint32_t)                     = 3;
        
        ep_mattr_ret_t
        EP_LOOKUP(ep_lookup_arg_t)              = 4;

        ep_mattr_ret_t
        EP_GETATTR(ep_mfile_arg_t)              = 5; 

        ep_mattr_ret_t
        EP_SETATTR(ep_setattr_arg_t)            = 6; 

        ep_readlink_ret_t
        EP_READLINK(ep_mfile_arg_t)             = 7;

        ep_mattr_ret_t
        EP_MKNOD(ep_mknod_arg_t)                = 8;

        ep_mattr_ret_t
        EP_MKDIR(ep_mkdir_arg_t)                = 9;

        ep_fid_ret_t
        EP_UNLINK(ep_unlink_arg_t)              = 10;

        ep_fid_ret_t
        EP_RMDIR(ep_rmdir_arg_t)                = 12;

        ep_mattr_ret_t
        EP_SYMLINK(ep_symlink_arg_t)            = 13;

        ep_fid_ret_t
        EP_RENAME(ep_rename_arg_t)              = 14;

        ep_readdir_ret_t
        EP_READDIR(ep_readdir_arg_t)            = 15;

        ep_read_block_ret_t
        EP_READ_BLOCK(ep_io_arg_t)              = 16;

        ep_io_ret_t
        EP_WRITE_BLOCK(ep_write_block_arg_t)    = 17;

        ep_mattr_ret_t
        EP_LINK(ep_link_arg_t)                  = 18;

    } = 1;
} = 0x20000001;
