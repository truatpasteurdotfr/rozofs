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

%#include <rozofs/rozofs.h>

/*
 * Common types
 */
typedef uint32_t        ep_uuid_t[ROZOFS_UUID_SIZE_NET];
typedef string          ep_name_t<ROZOFS_FILENAME_MAX>;
typedef string          ep_xattr_name_t<ROZOFS_XATTR_NAME_MAX>;
typedef string          ep_xattr_value_t<ROZOFS_XATTR_VALUE_MAX>;
typedef unsigned char   ep_xattr_list_t[ROZOFS_XATTR_LIST_MAX];
typedef string          ep_path_t<ROZOFS_PATH_MAX>;
typedef string          ep_link_t<ROZOFS_PATH_MAX>;
typedef char            ep_host_t[ROZOFS_HOSTNAME_MAX];
typedef char            ep_md5_t[ROZOFS_MD5_SIZE];

typedef string            ep_st_host_t<ROZOFS_HOSTNAME_MAX>;
typedef string            ep_epgw_host_t<ROZOFS_PATH_MAX>;


struct ep_gateway_t
{
    uint32_t  eid;
    uint32_t  nb_gateways;
    uint32_t  gateway_rank;
    uint32_t  hash_config;
};
 
enum ep_status_t {
    EP_SUCCESS = 0,
    EP_FAILURE = 1,
    EP_EMPTY   = 2,
    EP_FAILURE_EID_NOT_SUPPORTED =3,
    EP_NOT_SYNCED =4
};

union ep_status_ret_t switch (ep_status_t status) {
    case EP_FAILURE:    uint64_t error;
    default:            void;
};


struct  epgw_status_ret_t 
{
  struct ep_gateway_t hdr;
  ep_status_ret_t    status_gw;
};

struct ep_storage_t {
    ep_host_t       host;
    uint8_t         sid;
};



struct ep_cluster_t {
    uint16_t            cid;
    uint8_t             storages_nb;
    ep_storage_t        storages[SID_MAX];
};


union  ep_cluster_ret_t switch(ep_status_t status) {
  case EP_SUCCESS:     ep_cluster_t    cluster;
  case EP_FAILURE:    int             error;
  default:            void;
};

struct epgw_cluster_ret_t
{
  struct ep_gateway_t hdr;
  ep_cluster_ret_t    status_gw;
};

struct ep_storage_node_t {
    ep_host_t       host;
    uint8_t         sids_nb;
    uint8_t         sids[STORAGES_MAX_BY_STORAGE_NODE];
    uint16_t        cids[STORAGES_MAX_BY_STORAGE_NODE];
};

struct ep_export_t {
    uint32_t            hash_conf;
    uint32_t            eid;
    ep_md5_t            md5;
    ep_uuid_t           rfid;   /*root fid*/
    uint8_t             rl;     /* rozofs layout */
    uint8_t             storage_nodes_nb;
    ep_storage_node_t   storage_nodes[STORAGE_NODES_MAX];
};

union ep_mount_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    ep_export_t export;
    case EP_FAILURE:    int         error;
    default:            void;
};

struct epgw_mount_ret_t
{
  struct ep_gateway_t hdr;
  ep_mount_ret_t    status_gw;
};

struct ep_cnf_storage_node_t {
    string       host<ROZOFS_HOSTNAME_MAX>;
    uint8_t         sids_nb;
    uint8_t         sids[STORAGES_MAX_BY_STORAGE_NODE];
    uint16_t        cids[STORAGES_MAX_BY_STORAGE_NODE];
};

struct ep_conf_export_t {
    uint32_t            hash_conf;
    uint32_t            eid;
    ep_md5_t            md5;
    ep_uuid_t           rfid;   /*root fid*/
    uint8_t             rl;     /* rozofs layout */
    ep_cnf_storage_node_t   storage_nodes<>;
};

union ep_conf_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    ep_conf_export_t export;
    case EP_FAILURE:    int         error;
    default:            void;
};

struct epgw_conf_ret_t
{
  struct ep_gateway_t hdr;
  ep_conf_ret_t    status_gw;
};


struct ep_mattr_t {
    ep_uuid_t   fid;
    uint16_t    cid;
    uint8_t     sids[ROZOFS_SAFE_MAX];
    uint32_t    mode;
    uint32_t    uid;
    uint32_t    gid;
    uint16_t    nlink;
    uint64_t    ctime;
    uint64_t    atime;
    uint64_t    mtime;
    uint64_t    size;
    uint32_t    children;
};

union ep_mattr_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    ep_mattr_t  attrs;
    case EP_FAILURE:    int         error;
    case EP_EMPTY:      void;
    default:            void;
};


struct epgw_mattr_ret_t
{
  struct ep_gateway_t hdr;
  ep_mattr_ret_t    status_gw;
  ep_mattr_ret_t    parent_attr;
};

union ep_fid_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    ep_uuid_t   fid;
    case EP_FAILURE:    int         error;
    default:            void;
};


struct epgw_fid_ret_t
{
  struct ep_gateway_t hdr;
  ep_fid_ret_t    status_gw;
  ep_mattr_ret_t    parent_attr;

};

struct ep_lookup_arg_t {
    uint32_t    eid;
    ep_uuid_t   parent;
    ep_name_t   name;
};

struct  epgw_lookup_arg_t 
{
  struct ep_gateway_t hdr;
  ep_lookup_arg_t    arg_gw;
};

struct ep_mfile_arg_t {
    uint32_t    eid;
    ep_uuid_t   fid;
};
struct  epgw_mfile_arg_t 
{
  struct ep_gateway_t hdr;
  ep_mfile_arg_t    arg_gw;
};


struct ep_unlink_arg_t {
    uint32_t    eid;
    ep_uuid_t   pfid;
    ep_name_t   name;
};

struct  epgw_unlink_arg_t 
{
  struct ep_gateway_t hdr;
  ep_unlink_arg_t    arg_gw;
};

struct ep_rmdir_arg_t {
    uint32_t    eid;
    ep_uuid_t   pfid;
    ep_name_t   name;
};

struct  epgw_rmdir_arg_t 
{
  struct ep_gateway_t hdr;
  ep_rmdir_arg_t    arg_gw;
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

struct  epgw_statfs_ret_t 
{
  struct ep_gateway_t hdr;
  ep_statfs_ret_t    status_gw;
};

struct ep_setattr_arg_t {
    uint32_t    eid;
    uint32_t    to_set;
    ep_mattr_t  attrs;
};

struct  epgw_setattr_arg_t 
{
  struct ep_gateway_t hdr;
  ep_setattr_arg_t    arg_gw;
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

struct  epgw_readlink_ret_t 
{
  struct ep_gateway_t hdr;
  ep_readlink_ret_t    status_gw;
};

struct ep_mknod_arg_t {
    uint32_t    eid;
    ep_uuid_t   parent;
    ep_name_t   name;
    uint32_t    uid;
    uint32_t    gid;
    uint32_t    mode;
};

struct  epgw_mknod_arg_t 
{
  struct ep_gateway_t hdr;
  ep_mknod_arg_t    arg_gw;
};

struct ep_link_arg_t {
    uint32_t    eid;
    ep_uuid_t   inode;
    ep_uuid_t   newparent;
    ep_name_t   newname;
};

struct  epgw_link_arg_t 
{
  struct ep_gateway_t hdr;
  ep_link_arg_t    arg_gw;
};


struct ep_mkdir_arg_t {
    uint32_t    eid;
    ep_uuid_t   parent;
    ep_name_t   name;
    uint32_t    uid;
    uint32_t    gid;
    uint32_t    mode;
};

struct  epgw_mkdir_arg_t 
{
  struct ep_gateway_t hdr;
  ep_mkdir_arg_t    arg_gw;
};

struct ep_symlink_arg_t {
    uint32_t    eid;
    ep_link_t   link;
    ep_uuid_t   parent;
    ep_name_t   name;
};
struct  epgw_symlink_arg_t 
{
  struct ep_gateway_t hdr;
  ep_symlink_arg_t    arg_gw;
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
struct  epgw_readdir_arg_t 
{
  struct ep_gateway_t hdr;
  ep_readdir_arg_t    arg_gw;
};

union ep_readdir_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    dirlist_t       reply;
    case EP_FAILURE:    int             error;
    default:            void;
};

struct  epgw_readdir_ret_t 
{
  struct ep_gateway_t hdr;
  ep_readdir_ret_t    status_gw;
};

struct ep_rename_arg_t {
    uint32_t    eid;
    ep_uuid_t   pfid;
    ep_name_t   name;
    ep_uuid_t   npfid;
    ep_name_t   newname;
};
struct  epgw_rename_arg_t 
{
  struct ep_gateway_t hdr;
  ep_rename_arg_t    arg_gw;
};

struct ep_io_arg_t {
    uint32_t    eid;
    ep_uuid_t   fid;
    uint64_t    offset;
    uint32_t    length;
};

struct  epgw_io_arg_t 
{
  struct ep_gateway_t hdr;
  ep_io_arg_t    arg_gw;
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

struct  epgw_write_block_arg_t 
{
  struct ep_gateway_t hdr;
  ep_write_block_arg_t    arg_gw;
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


struct  epgw_read_block_ret_t 
{
  struct ep_gateway_t hdr;
  ep_read_block_ret_t    status_gw;
};

union ep_io_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    int64_t     length;
    case EP_FAILURE:    int         error;
    default:            void;
};
struct  epgw_io_ret_t 
{
  struct ep_gateway_t hdr;
  ep_io_ret_t    status_gw;
  ep_mattr_ret_t  attr;

};

struct ep_setxattr_arg_t {
    uint32_t          eid;
    ep_uuid_t         fid;
    ep_xattr_name_t   name;
    opaque            value<>;
    uint8_t           flags;
};

struct  epgw_setxattr_arg_t 
{
  struct ep_gateway_t hdr;
  ep_setxattr_arg_t    arg_gw;
};

struct ep_getxattr_arg_t {
    uint32_t          eid;
    ep_uuid_t         fid;
    ep_xattr_name_t   name;
    uint64_t          size;
};


struct  epgw_getxattr_arg_t 
{
  struct ep_gateway_t hdr;
  ep_getxattr_arg_t    arg_gw;
};
struct ep_getxattr_t {
    ep_xattr_value_t  value;
    uint64_t          size;
};

union ep_getxattr_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    opaque          value<>;
    case EP_FAILURE:    int             error;
    default:            void;
};


struct  epgw_getxattr_ret_t 
{
  struct ep_gateway_t hdr;
  ep_getxattr_ret_t    status_gw;
};


struct ep_removexattr_arg_t {
    uint32_t          eid;
    ep_uuid_t         fid;
    ep_xattr_name_t   name;
};
struct  epgw_removexattr_arg_t 
{
  struct ep_gateway_t hdr;
  ep_removexattr_arg_t    arg_gw;
};


struct ep_listxattr_arg_t {
    uint32_t          eid;
    ep_uuid_t         fid;
    uint64_t          size;
};

struct  epgw_listxattr_arg_t 
{
  struct ep_gateway_t hdr;
  ep_listxattr_arg_t    arg_gw;
};

union ep_listxattr_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    opaque          list<>;
    case EP_FAILURE:    int             error;
    default:            void;
};

struct  epgw_listxattr_ret_t 
{
  struct ep_gateway_t hdr;
  ep_listxattr_ret_t    status_gw;
};

struct ep_gw_host_conf_t  
{
  ep_epgw_host_t   host;
};
struct ep_gw_header_t {
  uint32_t export_id;
  uint32_t nb_gateways;
  uint32_t gateway_rank;
  uint32_t configuration_indice;
};


struct ep_gateway_configuration_t {
  ep_gw_header_t     hdr;
  ep_epgw_host_t     exportd_host;
  uint16_t           exportd_port;
  uint16_t           gateway_port;
  uint32_t           eid<>;  
  ep_gw_host_conf_t     gateway_host<>;
} ; 

union ep_gateway_configuration_ret_t switch (ep_status_t status) {
    case EP_SUCCESS:    ep_gateway_configuration_t    config;
    case EP_FAILURE:    int             error;
    default:            void;
};
struct ep_gw_gateway_configuration_ret_t
{
        struct ep_gateway_t hdr;
        ep_gateway_configuration_ret_t status_gw;
};

program EXPORT_PROGRAM {
    version EXPORT_VERSION {

        void
        EP_NULL(void)                           = 0;

        epgw_mount_ret_t
        EP_MOUNT(ep_path_t)                     = 1;

        epgw_status_ret_t
        EP_UMOUNT(uint32_t)                     = 2;

        epgw_statfs_ret_t
        EP_STATFS(uint32_t)                     = 3;
        
        epgw_mattr_ret_t
        EP_LOOKUP(epgw_lookup_arg_t)              = 4;

        epgw_mattr_ret_t
        EP_GETATTR(epgw_mfile_arg_t)              = 5; 

        epgw_mattr_ret_t
        EP_SETATTR(epgw_setattr_arg_t)            = 6; 

        epgw_readlink_ret_t
        EP_READLINK(epgw_mfile_arg_t)             = 7;

        epgw_mattr_ret_t
        EP_MKNOD(epgw_mknod_arg_t)                = 8;

        epgw_mattr_ret_t
        EP_MKDIR(epgw_mkdir_arg_t)                = 9;

        epgw_fid_ret_t
        EP_UNLINK(epgw_unlink_arg_t)              = 10;

        epgw_fid_ret_t
        EP_RMDIR(epgw_rmdir_arg_t)                = 12;

        epgw_mattr_ret_t
        EP_SYMLINK(epgw_symlink_arg_t)            = 13;

        epgw_fid_ret_t
        EP_RENAME(epgw_rename_arg_t)              = 14;

        epgw_readdir_ret_t
        EP_READDIR(epgw_readdir_arg_t)            = 15;

        epgw_read_block_ret_t
        EP_READ_BLOCK(epgw_io_arg_t)              = 16;

        epgw_mattr_ret_t
        EP_WRITE_BLOCK(epgw_write_block_arg_t)    = 17;

        epgw_mattr_ret_t
        EP_LINK(epgw_link_arg_t)                  = 18;

        epgw_status_ret_t
        EP_SETXATTR(epgw_setxattr_arg_t)          = 19;

        epgw_getxattr_ret_t
        EP_GETXATTR(epgw_getxattr_arg_t)          = 20;

        epgw_status_ret_t
        EP_REMOVEXATTR(epgw_removexattr_arg_t)    = 21;

        epgw_listxattr_ret_t
        EP_LISTXATTR(epgw_listxattr_arg_t)        = 22;

        epgw_cluster_ret_t
        EP_LIST_CLUSTER(uint16_t)                 = 23;

        epgw_conf_ret_t
        EP_CONF_STORAGE(ep_path_t)                        = 24;
               
        epgw_status_ret_t
        EP_POLL_CONF(ep_gateway_t)                        = 25;

        ep_gw_gateway_configuration_ret_t
        EP_CONF_EXPGW(ep_path_t)                           = 26;
        
    } = 1;
} = 0x20000001;
