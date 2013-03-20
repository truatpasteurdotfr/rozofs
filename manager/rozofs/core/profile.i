%module profile

%include <carrays.i>
%include <stdint.i>

%{
#define SWIG_FILE_WITH_INIT
#include <rozofs/rpc/epclient.h>
#include <rozofs/rpc/spclient.h>
#include <rozofs/rpc/mpclient.h>
%}

struct epp_estat_t {
    uint32_t eid;
    uint32_t vid;
    uint16_t bsize;
    uint64_t blocks;
    uint64_t bfree;
    uint64_t files;
    uint64_t ffree;
};
typedef struct epp_estat_t epp_estat_t;

struct epp_sstat_t {
    uint16_t sid;
    uint8_t status;
    uint64_t size;
    uint64_t free;
};
typedef struct epp_sstat_t epp_sstat_t;

struct epp_vstat_t {
    uint16_t vid;
    uint16_t bsize;
    uint64_t bfree;
    uint32_t nb_storages;
    epp_sstat_t sstats[2048];
};
typedef struct epp_vstat_t epp_vstat_t;

struct epp_profiler_t {
    uint64_t uptime;
    uint64_t now;
    char vers[20];
    uint32_t nb_volumes;
    epp_vstat_t vstats[16];
    uint32_t nb_exports;
    epp_estat_t estats[2048];
    uint64_t ep_mount[2];
    uint64_t ep_umount[2];
    uint64_t ep_statfs[2];
    uint64_t ep_lookup[2];
    uint64_t ep_getattr[2];
    uint64_t ep_setattr[2];
    uint64_t ep_readlink[2];
    uint64_t ep_mknod[2];
    uint64_t ep_mkdir[2];
    uint64_t ep_unlink[2];
    uint64_t ep_rmdir[2];
    uint64_t ep_symlink[2];
    uint64_t ep_rename[2];
    uint64_t ep_readdir[2];
    uint64_t ep_read_block[3];
    uint64_t ep_write_block[3];
    uint64_t ep_link[2];
    uint64_t ep_setxattr[2];
    uint64_t ep_getxattr[2];
    uint64_t ep_removexattr[2];
    uint64_t ep_listxattr[2];
    uint64_t export_lv1_resolve_entry[2];
    uint64_t export_lv2_resolve_path[2];
    uint64_t export_lookup_fid[2];
    uint64_t export_update_files[2];
    uint64_t export_update_blocks[2];
    uint64_t export_stat[2];
    uint64_t export_lookup[2];
    uint64_t export_getattr[2];
    uint64_t export_setattr[2];
    uint64_t export_link[2];
    uint64_t export_mknod[2];
    uint64_t export_mkdir[2];
    uint64_t export_unlink[2];
    uint64_t export_rmdir[2];
    uint64_t export_symlink[2];
    uint64_t export_readlink[2];
    uint64_t export_rename[2];
    uint64_t export_read[3];
    uint64_t export_read_block[2];
    uint64_t export_write_block[2];
    uint64_t export_setxattr[2];
    uint64_t export_getxattr[2];
    uint64_t export_removexattr[2];
    uint64_t export_listxattr[2];
    uint64_t export_readdir[2];
    uint64_t lv2_cache_put[2];
    uint64_t lv2_cache_get[2];
    uint64_t lv2_cache_del[2];
    uint64_t volume_balance[2];
    uint64_t volume_distribute[2];
    uint64_t volume_stat[2];
    uint64_t mdir_open[2];
    uint64_t mdir_close[2];
    uint64_t mdir_read_attributes[2];
    uint64_t mdir_write_attributes[2];
    uint64_t mreg_open[2];
    uint64_t mreg_close[2];
    uint64_t mreg_read_attributes[2];
    uint64_t mreg_write_attributes[2];
    uint64_t mreg_read_dist[2];
    uint64_t mreg_write_dist[2];
    uint64_t mslnk_open[2];
    uint64_t mslnk_close[2];
    uint64_t mslnk_read_attributes[2];
    uint64_t mslnk_write_attributes[2];
    uint64_t mslnk_read_link[2];
    uint64_t mslnk_write_link[2];
};
typedef struct epp_profiler_t epp_profiler_t;

struct spp_profiler_t {
    uint64_t uptime;
    uint64_t now;
    char vers[20];
    uint64_t stat[2];
    uint64_t ports[2];
    uint64_t remove[2];
    uint64_t read[3];
    uint64_t write[3];
    uint64_t truncate[3];
    uint16_t nb_io_processes;
    uint16_t io_process_ports[32];
    uint16_t nb_rb_processes;
    uint16_t rb_process_ports[32];
    uint16_t rbs_cids[32];
    uint8_t rbs_sids[32];
    uint64_t rb_files_current;
    uint64_t rb_files_total;
};
typedef struct spp_profiler_t spp_profiler_t;

struct mpp_profiler_t {
    uint64_t uptime;
    uint64_t now;
    char vers[20];
    uint64_t rozofs_ll_lookup[2];
    uint64_t rozofs_ll_forget[2];
    uint64_t rozofs_ll_getattr[2];
    uint64_t rozofs_ll_setattr[2];
    uint64_t rozofs_ll_readlink[2];
    uint64_t rozofs_ll_mknod[2];
    uint64_t rozofs_ll_mkdir[2];
    uint64_t rozofs_ll_unlink[2];
    uint64_t rozofs_ll_rmdir[2];
    uint64_t rozofs_ll_symlink[2];
    uint64_t rozofs_ll_rename[2];
    uint64_t rozofs_ll_open[2];
    uint64_t rozofs_ll_link[2];
    uint64_t rozofs_ll_read[3];
    uint64_t rozofs_ll_write[3];
    uint64_t rozofs_ll_flush[2];
    uint64_t rozofs_ll_release[2];
    uint64_t rozofs_ll_opendir[2];
    uint64_t rozofs_ll_readdir[2];
    uint64_t rozofs_ll_releasedir[2];
    uint64_t rozofs_ll_fsyncdir[2];
    uint64_t rozofs_ll_statfs[2];
    uint64_t rozofs_ll_setxattr[2];
    uint64_t rozofs_ll_getxattr[2];
    uint64_t rozofs_ll_listxattr[2];
    uint64_t rozofs_ll_removexattr[2];
    uint64_t rozofs_ll_access[2];
    uint64_t rozofs_ll_create[2];
    uint64_t rozofs_ll_getlk[2];
    uint64_t rozofs_ll_setlk[2];
    uint64_t rozofs_ll_ioctl[2];
};
typedef struct mpp_profiler_t mpp_profiler_t;

%include "rozofs/rpc/epclient.h"
%include "rozofs/rpc/spclient.h"
%include "rozofs/rpc/mpclient.h"

%array_functions(uint16_t, Uint16Array);
%array_functions(uint64_t, Uint64Array);
%array_functions(epp_vstat_t, EppVstatArray);
%array_functions(epp_estat_t, EppEstatArray);
%array_functions(epp_sstat_t, EppSstatArray);
