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
#include <rozofs/rpc/export_profiler.h>
#include <rozofs/common/export_track.h>
#include <rozofs/rpc/epproto.h>
#include <rozofs/rpc/mclient.h>

#include "config.h"
#include "export.h"
#include "cache.h"
#include "mdirent.h"
#include "xattr_main.h"

/** Max entries of lv1 directory structure (nb. of buckets) */
#define MAX_LV1_BUCKETS 1024
#define LV1_NOCREATE 0
#define LV1_CREATE 1

/** Default mode for export root directory */
#define EXPORT_DEFAULT_ROOT_MODE S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;



typedef struct cnxentry {
    mclient_t *cnx;
    list_t list;
} cnxentry_t;


int export_lv2_resolve_path(export_t *export, fid_t fid, char *path);

/**
* internal structure used for bitmap root_idx: only for directory
*/
#define DIRENT_MAX_IDX_FOR_EXPORT 4096
#define DIRENT_MASK_FOR_EXPORT (4096-1)
#define DIRENT_FILE_BYTE_BITMAP_SZ (DIRENT_MAX_IDX_FOR_EXPORT/8)
typedef struct _dirent_dir_root_idx_bitmap_t
{
   int dirty; /**< assert to one if the bitmap must be re-written on disk */
   char bitmap[DIRENT_FILE_BYTE_BITMAP_SZ];
} dirent_dir_root_idx_bitmap_t;

/*
 **__________________________________________________________________
 */
/**
* service to check if the bitmap for root_idx must be loaded

  @param lvl2 : level 2 entry
  @param fid : file id of the directory
  @param e:   pointer to the export structure
  
  @retval 0 on success
  @retval < 0 on error
*/

int export_dir_load_root_idx_bitmap(export_t *e,fid_t fid,lv2_entry_t *lvl2)
{
   int fd = -1;
   char node_path[PATH_MAX];
   char lv3_path[PATH_MAX];
   dirent_dir_root_idx_bitmap_t *bitmap_p;

   if (lvl2->dirent_root_idx_p != NULL)
   {
     /*
     ** already loaded
     */
     return 0;   
   }
   /*
   ** the entry must be a directory
   */
   if (!S_ISDIR(lvl2->attributes.s.attrs.mode)) return 0;
   /*
   ** allocate the memory
   */
   lvl2->dirent_root_idx_p = malloc(sizeof(dirent_dir_root_idx_bitmap_t));
   if (lvl2->dirent_root_idx_p == NULL) goto error;
   bitmap_p = (dirent_dir_root_idx_bitmap_t*)lvl2->dirent_root_idx_p;
   /*
   ** read the bitmap from disk
   */    
   if (export_lv2_resolve_path(e, fid, node_path) != 0) goto error;
   sprintf(lv3_path, "%s/%s", node_path, MDIR_ATTRS_FNAME);   
   if ((fd = open(lv3_path, O_RDONLY | O_NOATIME, S_IRWXU)) < 0) 
   {
     goto error;
   }
   ssize_t len = pread(fd,bitmap_p->bitmap,DIRENT_FILE_BYTE_BITMAP_SZ,0);
   if (len != DIRENT_FILE_BYTE_BITMAP_SZ) goto error;
   /*
   ** clear the dirty bit
   */
   bitmap_p->dirty = 0;
   /*
   ** close the file
   */
   close(fd);
   return 0;
   
error:
   if (fd != -1) close(fd);
   if (lvl2->dirent_root_idx_p != NULL)
   {
      free(lvl2->dirent_root_idx_p);
      lvl2->dirent_root_idx_p = NULL;  
   }
   return -1;
}
/*
**__________________________________________________________________
*/
/**
*   update the root_idx bitmap in memory

   @param ctx_p: pointer to the level2 cache entry
   @param root_idx : root index to update
   @param set : assert to 1 when the root_idx is new/ 0 for removing
   

*/
void export_dir_update_root_idx_bitmap(void *ctx_p,int root_idx,int set)
{
    uint16_t byte_idx;
    int bit_idx ;
    dirent_dir_root_idx_bitmap_t *bitmap_p;
    
    if (ctx_p == NULL) return;
    
    bitmap_p = (dirent_dir_root_idx_bitmap_t*)ctx_p;
    
    if (root_idx >DIRENT_MAX_IDX_FOR_EXPORT) return;
    
    byte_idx = root_idx/8;
    bit_idx =  root_idx%8;
    if (set)
    {
       if (bitmap_p->bitmap[byte_idx] & (1<<bit_idx)) return;
       bitmap_p->bitmap[byte_idx] |= 1<<bit_idx;    
    }
    else
    {
       bitmap_p->bitmap[byte_idx] &=~(1<<bit_idx);        
    }
    bitmap_p->dirty = 1;
}
/*
**__________________________________________________________________
*/
/**
*   check the presence of a root_idx  in the bitmap 

   @param ctx_p: pointer to the level2 cache entry
   @param root_idx : root index to update

  @retval 1 asserted
  @retval 0 not set   

*/
int export_dir_check_root_idx_bitmap_bit(void *ctx_p,int root_idx)
{
    uint16_t byte_idx;
    int bit_idx ;
    dirent_dir_root_idx_bitmap_t *bitmap_p;
    
    if (ctx_p == NULL) return 1;
    
    bitmap_p = (dirent_dir_root_idx_bitmap_t*)ctx_p;
    if (root_idx >DIRENT_MAX_IDX_FOR_EXPORT) return 1;
    
    byte_idx = root_idx/8;
    bit_idx =  root_idx%8;

    if (bitmap_p->bitmap[byte_idx] & (1<<bit_idx)) 
    {
      return 1;
    }
    return 0;
}
/*
**__________________________________________________________________
*/
/**
* service to flush on disk the root_idx bitmap if it is dirty

  @param bitmap_p : pointer to the root_idx bitmap
  @param fid : file id of the directory
  @param e:   pointer to the export structure
  
  @retval 0 on success
  @retval < 0 on error
*/

int export_dir_flush_root_idx_bitmap(export_t *e,fid_t fid,dirent_dir_root_idx_bitmap_t *bitmap_p)
{
   int fd = -1;
   char node_path[PATH_MAX];
   char lv3_path[PATH_MAX];

   if (bitmap_p == NULL)
   {
     /*
     ** nothing to flush
     */
     return 0;   
   }
   if (bitmap_p->dirty == 0) return 0;
   /*
   ** bitmap has changed :write the bitmap on disk
   */    
   if (export_lv2_resolve_path(e, fid, node_path) != 0) goto error;
   
   sprintf(lv3_path, "%s/%s", node_path, MDIR_ATTRS_FNAME);   
   if ((fd = open(lv3_path, O_WRONLY | O_CREAT | O_NOATIME, S_IRWXU)) < 0) {
        goto error;
   }
   ssize_t len = pwrite(fd,bitmap_p->bitmap,DIRENT_FILE_BYTE_BITMAP_SZ,0);
   if (len != DIRENT_FILE_BYTE_BITMAP_SZ) goto error;
   /*
   ** clear the dirty bit
   */
   bitmap_p->dirty = 0;
   /*
   ** close the file
   */
   close(fd);
   return 0;
   
error:
   if (fd != -1) close(fd);
   return -1;
}

/*
 **__________________________________________________________________
 */

/** get the lv1 directory.
 *
 * lv1 entries are first level directories of an export root named by uint32_t
 * string value and used has entry of a hash table storing the export
 * meta data files.
 *
 * @param root_path: root path of the exportd
 * @param slice: value of the slice
 *
 * @return 0 on success otherwise -1
 */
static inline int mstor_slice_resolve_entry(char *root_path, uint32_t slice) {
    char path[PATH_MAX];
    sprintf(path, "%s/%"PRId32"", root_path, slice);
    if (access(path, F_OK) == -1) {
        if (errno == ENOENT) {
            /*
             ** it is the fisrt time we access to the slice
             **  we need to create the level 1 directory and the 
             ** timestamp file
             */
            if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) != 0) {
                severe("mkdir (%s): %s", path, strerror(errno));
                return -1;
            }
            //          mstor_ts_srv_create_slice_timestamp_file(export,slice); 
            return 0;
        }
        /*
         ** any other error
         */
        severe("access (%s): %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

/*
 **__________________________________________________________________
 */

/** get the subslice directory index.
 *
 * lv1 entries are first level directories of an export root named by uint32_t
 * string value and used has entry of a hash table storing the export
 * meta data files.
 *
 * @param root_path: root path of the exportd
 * @param fid: the search fid
 *
 * @return 0 on success otherwise -1
 */
static inline int mstor_subslice_resolve_entry(char *root_path, fid_t fid, uint32_t slice, uint32_t subslice) {
    char path[PATH_MAX];


    sprintf(path, "%s/%d", root_path, slice);
    if (access(path, F_OK) == -1) {
        if (errno == ENOENT) {
            /*
             ** it is the fisrt time we access to the subslice
             **  we need to create the associated directory 
             */
            if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) != 0) {
                severe("mkdir (%s): %s", path, strerror(errno));
                return -1;
            }
            return 0;
        }
        severe("access (%s): %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

/** build a full path based on export root and fid of the lv2 file
 *
 * lv2 is the second level of files or directories in storage of metadata
 * they are acceded thru mreg or mdir API according to their type.
 *
 * @param root_path: root path of the exportd
 * @param fid: the fid we are looking for
 * @param path: the path to fill in
 */

static inline int export_lv2_resolve_path_internal(char *root_path, fid_t fid, char *path) {
    uint32_t slice;
    uint32_t subslice;
    char str[37];

    /*
     ** extract the slice and subsclie from the fid
     */
    mstor_get_slice_and_subslice(fid, &slice, &subslice);
#if 0 // subslice is not used anymore
    /*
     ** check the existence of the slice directory: create it if it does not exist
     */
    if (mstor_slice_resolve_entry(root_path, slice) < 0) {
        goto error;
    }
    /*
     ** check the existence of the subslice directory: create it if it does not exist
     */
    if (mstor_subslice_resolve_entry(root_path, fid, slice, subslice) < 0) {
        goto error;
    }
#endif
    /*
     ** convert the fid in ascii
     */
    uuid_unparse(fid, str);
    sprintf(path, "%s/%d/%s", root_path, slice, str);
    return 0;

    return -1;
}

/** build a full path based on export root and fid of the lv2 file
 *
 * lv2 is the second level of files or directories in storage of metadata
 * they are acceded thru mreg or mdir API according to their type.
 *
 * @param export: the export we are searching on
 * @param fid: the fid we are looking for
 * @param path: the path to fill in
 */

int export_lv2_resolve_path(export_t *export, fid_t fid, char *path) {
    int ret;

    START_PROFILING(export_lv2_resolve_path);

    ret = export_lv2_resolve_path_internal(export->root, fid, path);

    STOP_PROFILING(export_lv2_resolve_path);
    return ret;
}


/**
*  open the parent directory

   @param e : pointer to the export structure
   @param parent : fid of the parent directory
   
   @retval > 0 : fd of the directory
   @retval < 0 error
*/
int export_open_parent_directory(export_t *e,fid_t parent)
{
    int fd = -1;
    
    dirent_current_eid = e->eid;
    char node_path[PATH_MAX];
    if (export_lv2_resolve_path(e, parent, node_path) != 0)
        goto out;
   if ((fd = open(node_path, O_RDONLY | O_NOATIME, S_IRWXU)) < 0) {
        goto out;
    }
out:
   return fd;
}



/** update the number of files in file system
 *
 * @param e: the export to update
 * @param n: number of files
 *
 * @return 0 on success -1 otherwise
 */
static int export_update_files(export_t *e, int32_t n) {
    int status = -1;
    START_PROFILING(export_update_files);

    if (n<0) {
      n = -n;
      /*
      ** Releasing more files than existing !!!
      */
      if (n > e->fstat.files) {
        severe("export %s blocks %"PRIu64" files %"PRIu64". Releasing %d files",
	       e->root, e->fstat.blocks, e->fstat.files, n); 
        n = e->fstat.files;
      }
      e->fstat.files -= n;
    }
    else {
      e->fstat.files += n;
    }
    if (pwrite(e->fdstat, &e->fstat, sizeof (export_fstat_t), 0)
            != sizeof (export_fstat_t))
        goto out;

    status = 0;
out:
    STOP_PROFILING(export_update_files);
    return status;
}

/** update the number of blocks in file system
 *
 * @param e: the export to update
 * @param n: number of blocks
 *
 * @return 0 on success -1 otherwise
 */
static int export_update_blocks(export_t * e, int32_t n) {
    int status = -1;

    if (n == 0) return 0;
    
    START_PROFILING(export_update_blocks);
    
    /*
    ** Releasing some blocks 
    */
    if (n<0) {
    
      n = -n;

      /*
      ** Releasing more blocks than allocated !!!
      */
      if (n > e->fstat.blocks) {
        severe("export %s blocks %"PRIu64" files %"PRIu64". Releasing %d blocks",
	       e->root, e->fstat.blocks, e->fstat.files, n); 
        n = e->fstat.blocks;
      }

      e->fstat.blocks -= n;
    }
    else {

      if (e->hquota > 0 && e->fstat.blocks + n > e->hquota) {
          warning("quota exceed: %"PRIu64" over %"PRIu64"", e->fstat.blocks + n,
                  e->hquota);
          errno = EDQUOT;
          goto out;
      }

      e->fstat.blocks += n;      
    }
    if (pwrite(e->fdstat, &e->fstat, sizeof (export_fstat_t), 0)
            != sizeof (export_fstat_t))
        goto out;

    status = 0;
out:
    STOP_PROFILING(export_update_blocks);
    return status;
}

/** constants of the export */
typedef struct export_const {
    char version[20]; ///< rozofs version
    fid_t rfid; ///< root id
} export_const_t;

int export_is_valid(const char *root) {
    char path[PATH_MAX];
    char fstat_path[PATH_MAX];
    char const_path[PATH_MAX];
    int i;

    if (!realpath(root, path))
        return -1;

    if (access(path, R_OK | W_OK | X_OK) != 0)
        return -1;
    for (i = 1 ; i <= EXPORT_SLICE_PROCESS_NB;i++)
    {
  #if 0 // not needed for the control
      // check trash directory
      sprintf(trash_path, "%s/%s_%d", path, TRASH_DNAME,i);
      if (access(trash_path, F_OK) != 0)
          return -1;
  #endif
      // check fstat file
      sprintf(fstat_path, "%s/%s_%d", path, FSTAT_FNAME,i);
      if (access(fstat_path, F_OK) != 0)
          return -1;
    }
    // check const file
    sprintf(const_path, "%s/%s", path, CONST_FNAME);
    if (access(const_path, F_OK) != 0)
        return -1;

    return 0;
}


int export_create(const char *root,export_t * e) {
    const char *version = VERSION;
    char path[PATH_MAX];
    char trash_path[PATH_MAX];
    char fstat_path[PATH_MAX];
    char const_path[PATH_MAX];
    char root_path[PATH_MAX];
    char slice_path[PATH_MAX];
    export_fstat_t est;
    export_const_t ect;
    int fd = -1;
//    mattr_t root_attrs;
    mdir_t root_mdir;
    ext_mattr_t ext_attrs;
    uint32_t pslice = 0;
    dirent_dir_root_idx_bitmap_t root_idx_bitmap;

    memset(&root_idx_bitmap,0,sizeof(dirent_dir_root_idx_bitmap_t));
    
    e->trk_tb_p = NULL;

    int i;

    if (!realpath(root, path))
        return -1;
    if ( expgwc_non_blocking_conf.slave == 0)
    {
      /*
      ** create the tracking context of the export
      */
      e->trk_tb_p = exp_create_attributes_tracking_context(e->eid,(char*)root,1);
      if (e->trk_tb_p == NULL)
      {
	 severe("error on tracking context allocation: %s\n",strerror(errno));
	 return -1;  
      }

      for (i = 0 ; i <= EXPORT_SLICE_PROCESS_NB;i++)
      {
	// create trash directory
	sprintf(trash_path, "%s/%s_%d", path, TRASH_DNAME,i);
	if (mkdir(trash_path, S_IRUSR | S_IWUSR | S_IXUSR) < 0) {
            return -1;
	}
      }
      /*
      ** create the directories slices
      */
      for (i = 0 ; i <= MAX_SLICE_NB;i++)
      {
	// create slices for directories
	sprintf(slice_path, "%s/%d", path,i);
	if (mkdir(slice_path, S_IRUSR | S_IWUSR | S_IXUSR) < 0) {
            return -1;
	}
      }
            
      for (i = 0 ; i <= EXPORT_SLICE_PROCESS_NB;i++)
      {
	// create fstat file
	sprintf(fstat_path, "%s/%s_%d", path, FSTAT_FNAME,i);
	if ((fd = open(fstat_path, O_RDWR | O_CREAT, S_IRWXU)) < 1) {
            return -1;
	}

	est.blocks = est.files = 0;
	if (write(fd, &est, sizeof (export_fstat_t)) != sizeof (export_fstat_t)) {
            close(fd);
            return -1;
	}
	close(fd);
      }

      //create root
      memset(&ext_attrs, 0, sizeof (ext_attrs));
//      uuid_generate(ext_attrs.s.attrs.fid);
      ext_attrs.s.attrs.cid = 0;
      memset(ext_attrs.s.attrs.sids, 0, ROZOFS_SAFE_MAX * sizeof (sid_t));

      // Put the default mode for the root directory
      ext_attrs.s.attrs.mode = EXPORT_DEFAULT_ROOT_MODE;

      ext_attrs.s.attrs.nlink = 2;
      ext_attrs.s.attrs.uid = 0; // root
      ext_attrs.s.attrs.gid = 0; // root
      if ((ext_attrs.s.attrs.ctime = ext_attrs.s.attrs.atime = ext_attrs.s.attrs.mtime = time(NULL)) == -1)
          return -1;
      ext_attrs.s.attrs.size = ROZOFS_DIR_SIZE;
      // Set children count to 0
      ext_attrs.s.attrs.children = 0;

      if(exp_attr_create(e->trk_tb_p,pslice,&ext_attrs,ROZOFS_DIR,NULL) < 0)
      {
        severe("cannot allocate an inode for root directory");
	return -1;
      }
      /*
       ** create the slice and subslice directory for root if they don't exist
       ** and then create the "fid" directory or the root
       */
      if (export_lv2_resolve_path_internal(path, ext_attrs.s.attrs.fid, root_path) != 0)
          return -1;
	  
      if (mkdir(root_path, S_IRUSR | S_IWUSR | S_IXUSR) < 0) {
          return -1;
      }
      // open the root mdir
      if (mdir_open(&root_mdir, root_path) != 0) {
          severe("cannot open %s: %s",root_path,strerror(errno));
          return -1;
      }

      // Initialize the dirent level 0 cache
      dirent_cache_level0_initialize();
      dirent_wbcache_init();
      dirent_wbcache_disable();

      // create "." ".." lv3 entries
      if (put_mdirentry(&root_idx_bitmap,root_mdir.fdp, ext_attrs.s.attrs.fid, ".", ext_attrs.s.attrs.fid, S_IFDIR | S_IRWXU) != 0) {
          severe("put_mdirentry failure %s",root_path);
          mdir_close(&root_mdir);
          return -1;
      }
      if (put_mdirentry(&root_idx_bitmap,root_mdir.fdp, ext_attrs.s.attrs.fid, "..", ext_attrs.s.attrs.fid, S_IFDIR | S_IRWXU) != 0) {
          severe("put_mdirentry failure %s",root_path);
          mdir_close(&root_mdir);
          return -1;
      }
      /*
      ** write root idx bitmap on disk
      */
      
       ssize_t lenbit = pwrite(root_mdir.fdattrs,root_idx_bitmap.bitmap,DIRENT_FILE_BYTE_BITMAP_SZ,0);
       if (lenbit != DIRENT_FILE_BYTE_BITMAP_SZ)
       {
          severe("write root_idx bitmap failure %s",root_path);
          mdir_close(&root_mdir);
          return -1;       
       }
	  mdir_close(&root_mdir);

      // create const file.
      memset(&ect, 0, sizeof (export_const_t));
      uuid_copy(ect.rfid, ext_attrs.s.attrs.fid);
      strncpy(ect.version, version, 20);
      sprintf(const_path, "%s/%s", path, CONST_FNAME);
      if ((fd = open(const_path, O_RDWR | O_CREAT, S_IRWXU)) < 1) {
          severe("open failure for %s: %s",const_path,strerror(errno));
          return -1;
      }

      if (write(fd, &ect, sizeof (export_const_t)) != sizeof (export_const_t)) {
          severe("write failure for %s: %s",const_path,strerror(errno));
          close(fd);
          return -1;
      }
      close(fd);
    }
    return 0;
}

static void *load_trash_dir_thread(void *v) {

    export_t *export = (export_t*) v;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    // Load files to delete in trash list
    if (export_load_rmfentry(export) != 0) {
        severe("export_load_rmfentry failed: %s", strerror(errno));
        return 0;
    }

    info("Load trash directory pthread completed successfully (eid=%d)",
            export->eid);

    return 0;
}

int export_initialize(export_t * e, volume_t *volume, ROZOFS_BSIZE_E bsize,
        lv2_cache_t *lv2_cache, uint32_t eid, const char *root, const char *md5,
        uint64_t squota, uint64_t hquota) {

    char fstat_path[PATH_MAX];
    char const_path[PATH_MAX];
    char root_path[PATH_MAX];
    export_const_t ect;
    int fd = -1;
    int i = 0;
    /*
    ** do it for eid if the process is master. For the slaves do it for
    ** the eid that are in their scope only
    */
    if (exportd_is_master()== 0) 
    {   
      if (exportd_is_eid_match_with_instance(eid) ==0) return 0;
    }

    if (!realpath(root, e->root))
    {
        severe("realpath failure for %s : %s",root,strerror(errno));
        return -1;
    }
    e->eid = eid;
    e->volume = volume;
    e->bsize = bsize;
    e->lv2_cache = lv2_cache;
    e->layout = volume->layout; // Layout used for this volume
    /*
    ** init of the replication context
    */
    {
      int k;
      for (k = 0; k < EXPORT_GEO_MAX_CTX; k++)
      {
        e->geo_replication_tb[k] = geo_rep_init(eid,k,(char*)root);
	if (e->geo_replication_tb[k] == NULL)
	{
	   return -1;
	}
	geo_rep_dbg_add(e->geo_replication_tb[k]);      
      }    
    }
    /*
    ** create the tracking context of the export
    */
     if (e->trk_tb_p == NULL)
     {
       e->trk_tb_p = exp_create_attributes_tracking_context(e->eid,(char*)root,1);
       if (e->trk_tb_p == NULL)
       {
	  severe("error on tracking context allocation: %s\n",strerror(errno));
	  return -1;  
       }
     }

    // Initialize the dirent level 0 cache
    dirent_cache_level0_initialize();
    dirent_wbcache_init();

    if (strlen(md5) == 0) {
        memcpy(e->md5, ROZOFS_MD5_NONE, ROZOFS_MD5_SIZE);
    } else {
        memcpy(e->md5, md5, ROZOFS_MD5_SIZE);
    }
    e->squota = squota;
    e->hquota = hquota;

    // open the export_stat file an load it
    sprintf(fstat_path, "%s/%s_%d", e->root, FSTAT_FNAME,(int)expgwc_non_blocking_conf.instance);
    if ((e->fdstat = open(fstat_path, O_RDWR)) < 0)
    {
        severe("open failure for %s : %s",fstat_path,strerror(errno));
        return -1;
    }
    if (pread(e->fdstat, &e->fstat, sizeof (export_fstat_t), 0)
            != sizeof (export_fstat_t))
        return -1;

    // Register the root
    sprintf(const_path, "%s/%s", e->root, CONST_FNAME);
    if ((fd = open(const_path, O_RDWR, S_IRWXU)) < 1) {
        severe("open failure for %s : %s",const_path,strerror(errno));
        return -1;
    }

    if (read(fd, &ect, sizeof (export_const_t)) != sizeof (export_const_t)) {
        close(fd);
        return -1;
    }
    close(fd);
    uuid_copy(e->rfid, ect.rfid);

    if (export_lv2_resolve_path(e, e->rfid, root_path) != 0) {
        severe("open failure for %s : %s",root_path,strerror(errno));
        close(e->fdstat);
        return -1;
    }

    if (!lv2_cache_put(e->trk_tb_p,e->lv2_cache, e->rfid)) {
        severe("open failure for %s : %s",root_path,strerror(errno));
        close(e->fdstat);
        return -1;
    }

    // For each trash bucket 
    for (i = 0; i < RM_MAX_BUCKETS; i++) {
        // Initialize list of files to delete
        list_init(&e->trash_buckets[i].rmfiles);
        // Initialize lock for the list of files to delete
        if ((errno = pthread_rwlock_init(&e->trash_buckets[i].rm_lock, NULL)) != 0) {
            severe("pthread_rwlock_init failed: %s", strerror(errno));
            return -1;
        }
    }

    // Initialize pthread for load files to remove
    if ((errno = pthread_create(&e->load_trash_thread, NULL,
            load_trash_dir_thread, e)) != 0) {
        severe("can't create load trash pthread: %s", strerror(errno));
        return -1;
    }

    return 0;
}

void export_release(export_t * e) {
    close(e->fdstat);
    // TODO set members to clean values
}

int export_stat(export_t * e, ep_statfs_t * st) {
    int status = -1;
    struct statfs stfs;
    volume_stat_t vstat;
    START_PROFILING_EID(export_stat,e->eid);

    st->bsize = ROZOFS_BSIZE_BYTES(e->bsize);
    if (statfs(e->root, &stfs) != 0)
        goto out;

    // may be ROZOFS_FILENAME_MAX should be stfs.f_namelen
    //st->namemax = stfs.f_namelen;
    st->namemax = ROZOFS_FILENAME_MAX;
    st->ffree = stfs.f_ffree;
    st->blocks = e->fstat.blocks;
    volume_stat(e->volume, &vstat);

    /* Volume statistics are given on 1024 block units */
    vstat.bfree /= (4<<e->bsize);

    if (e->hquota > 0) {
        if (e->hquota < vstat.bfree) {
            st->bfree = e->hquota - st->blocks;
        } else {
            st->bfree = vstat.bfree - st->blocks;
        }
    } else {
        st->bfree = vstat.bfree;
    }
    //st->bfree = e->hquota > 0 && e->hquota < vstat.bfree ? e->hquota : vstat.bfree;
    // blocks store in export stat file is the number of currently stored blocks
    // blocks in estat_t is the total number of blocks (see struct statvfs)
    // rozofs does not have a constant total number of blocks
    // it depends on usage made of storage (through other services)
    st->blocks += st->bfree;
    st->files = e->fstat.files;

    status = 0;
out:
    STOP_PROFILING_EID(export_stat,e->eid);
    return status;
}
/*
**__________________________________________________________________
*/
int export_lookup(export_t *e, fid_t pfid, char *name, mattr_t *attrs,mattr_t *pattrs) {
    int status = -1;
    lv2_entry_t *plv2 = 0;
    lv2_entry_t *lv2 = 0;
    fid_t child_fid;
    uint32_t child_type;
    int fdp = -1;     /* file descriptor of the parent directory */
    START_PROFILING(export_lookup);

    // get the lv2 parent
    if (!(plv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, pfid))) {
        goto out;
    }
    /*
    ** load the root_idx bitmap of the old parent
    */
    export_dir_load_root_idx_bitmap(e,pfid,plv2);
    /*
    ** copy the parent attributes
    */
    memcpy(pattrs, &plv2->attributes.s.attrs, sizeof (mattr_t));
    /*
    ** check direct access
    */
    if (strncmp(name,"@rozofs@",8) == 0)
    {
        fid_t fid_direct;
	int ret;
	lv2_entry_t exp_fake_lv2_entry;
	
	ret = uuid_parse(&name[8],fid_direct);
	if (ret < 0)
	{
	  errno = EINVAL;
	  goto out;
	}
	/*
	** read the attribute from disk
	*/
      lv2 = &exp_fake_lv2_entry;
      if (exp_meta_get_object_attributes(e->trk_tb_p,fid_direct,lv2) < 0)
      {
	/*
	** cannot get the attributes: need to log the returned errno
	*/
	errno = ENOENT;
	goto out;
      } 
       memcpy(attrs, &lv2->attributes.s.attrs, sizeof (mattr_t));
       status = 0;  
       goto out;      
    }

    fdp = export_open_parent_directory(e,pfid);
    if (fdp == -1) goto out;
    if (get_mdirentry(plv2->dirent_root_idx_p,fdp, pfid, name, child_fid, &child_type) != 0) {
        goto out;
    }

    // get the lv2
    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, child_fid))) {
        /*
         ** It might be possible that the file is still referenced in the dirent file but 
         ** not present on disk: its FID has been released (and the associated file deleted)
         ** In that case when attempt to read that fid file, we get a ENOENT error.
         ** So for that particular case, we remove the entry from the dirent file
         **
         **  open point : that issue is not related to regular file but also applied to directory
         ** 
         */
        int xerrno;
        uint32_t type;
        fid_t fid;
        if (errno == ENOENT) {
            /*
             ** save the initial errno and remove that entry
             */
            xerrno = errno;
            del_mdirentry(plv2->dirent_root_idx_p,fdp, pfid, name, fid, &type);
            errno = xerrno;
        }

        goto out;
    }

    memcpy(attrs, &lv2->attributes.s.attrs, sizeof (mattr_t));

    status = 0;
out:
    /*
    ** check if parent root idx bitmap must be updated
    */
    if (plv2 != NULL)export_dir_flush_root_idx_bitmap(e,pfid,plv2->dirent_root_idx_p);
    /*
    ** close the parent directory
    */
    if (fdp != -1) close(fdp);
    STOP_PROFILING(export_lookup);
    return status;
}
/*
**__________________________________________________________________
*/
/** get attributes of a managed file
 *
 * @param e: the export managing the file
 * @param fid: the id of the file
 * @param attrs: attributes to fill.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_getattr(export_t *e, fid_t fid, mattr_t *attrs) {
    int status = -1;
    lv2_entry_t *lv2 = 0;
    START_PROFILING(export_getattr);

    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid))) {
        severe("export_getattr failed: %s", strerror(errno));
        goto out;
    }
    memcpy(attrs, &lv2->attributes.s.attrs, sizeof (mattr_t));

    status = 0;
out:
    STOP_PROFILING(export_getattr);
    return status;
}
/*
**__________________________________________________________________
*/
/** set attributes of a managed file
 *
 * @param e: the export managing the file
 * @param fid: the id of the file
 * @param attrs: attributes to set.
 * @param to_set: fields to set in attributes
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_setattr(export_t *e, fid_t fid, mattr_t *attrs, int to_set) {
    int status = -1;
    lv2_entry_t *lv2 = 0;
    int bbytes = ROZOFS_BSIZE_BYTES(e->bsize);

    START_PROFILING(export_setattr);

    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid))) {
        goto out;
    }

    if ((to_set & EXPORT_SET_ATTR_SIZE) && S_ISREG(lv2->attributes.s.attrs.mode)) {
        
        // Check new file size
        if (attrs->size >= ROZOFS_FILESIZE_MAX) {
            errno = EFBIG;
            goto out;
        }

        uint64_t nrb_new = ((attrs->size + bbytes - 1) / bbytes);
        uint64_t nrb_old = ((lv2->attributes.s.attrs.size + bbytes - 1) / bbytes);
		
        if (export_update_blocks(e, ((int32_t) nrb_new - (int32_t) nrb_old))
                != 0)
            goto out;

        lv2->attributes.s.attrs.size = attrs->size;
    }

    if (to_set & EXPORT_SET_ATTR_MODE)
        lv2->attributes.s.attrs.mode = attrs->mode;
    if (to_set & EXPORT_SET_ATTR_UID)
        lv2->attributes.s.attrs.uid = attrs->uid;
    if (to_set & EXPORT_SET_ATTR_GID)
        lv2->attributes.s.attrs.gid = attrs->gid;    
    if (to_set & EXPORT_SET_ATTR_ATIME)
        lv2->attributes.s.attrs.atime = attrs->atime;
    if (to_set & EXPORT_SET_ATTR_MTIME)
        lv2->attributes.s.attrs.mtime = attrs->mtime;
    
    lv2->attributes.s.attrs.ctime = time(NULL);

    status = export_lv2_write_attributes(e->trk_tb_p,lv2);
out:
    STOP_PROFILING(export_setattr);
    return status;
}
/*
**__________________________________________________________________
*/
/** create a hard link
 *
 * @param e: the export managing the file
 * @param inode: the id of the file we want to be link on
 * @param newparent: parent od the new file (the link)
 * @param newname: the name of the new file
 * @param[out] attrs: mattr_t to fill (child attributes used by upper level functions)
 * @param[out] pattrs: mattr_t to fill (parent attributes)
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_link(export_t *e, fid_t inode, fid_t newparent, char *newname, mattr_t *attrs,mattr_t *pattrs) {
    int status = -1;
    lv2_entry_t *target = NULL;
    lv2_entry_t *plv2 = NULL;
    fid_t child_fid;
    uint32_t child_type;
    int fdp= -1;

    START_PROFILING(export_link);

    // Get the lv2 inode
    if (!(target = export_lookup_fid(e->trk_tb_p,e->lv2_cache, inode)))
        goto out;

    // Verify that the target is not a directory
    if (S_ISDIR(target->attributes.s.attrs.mode)) {
        errno = EPERM;
        goto out;
    }

    // Get the lv2 parent
    if (!(plv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, newparent)))
        goto out;
    /*
    ** load the root_idx bitmap of the old parent
    */
    export_dir_load_root_idx_bitmap(e,newparent,plv2);

    // Verify that the mdirentry does not already exist
    fdp = export_open_parent_directory(e,newparent);
    if (fdp == -1) 
       goto out;
    if (get_mdirentry(plv2->dirent_root_idx_p,fdp, newparent, newname, child_fid, &child_type) != -1) {
        errno = EEXIST;
        goto out;
    }
    /*
     ** nothing has been found, need to check the read only flag:
     ** that flag is asserted if some parts of dirent files are unreadable 
     */
    if (DIRENT_ROOT_IS_READ_ONLY()) {
        errno = EIO;
        goto out;
    }
    /*
    ** update the bit in the root_idx bitmap of the parent directory
    */
    uint32_t hash1,hash2;
    int root_idx;
    int len;
    
    hash1 = filename_uuid_hash_fnv(0, newname,newparent, &hash2, &len);
    root_idx = hash1 & DIRENT_MASK_FOR_EXPORT;
    export_dir_update_root_idx_bitmap(plv2->dirent_root_idx_p,root_idx,1);
    if (export_dir_flush_root_idx_bitmap(e,newparent,plv2->dirent_root_idx_p) < 0)
    {
       errno = EPROTO; 
       goto out;
    }
    // Put the new mdirentry
    if (put_mdirentry(plv2->dirent_root_idx_p,fdp, newparent, newname, target->attributes.s.attrs.fid, target->attributes.s.attrs.mode) != 0)
        goto out;

    // Update nlink and ctime for inode
    target->attributes.s.attrs.nlink++;
    target->attributes.s.attrs.ctime = time(NULL);

    // Write attributes of target
    if (export_lv2_write_attributes(e->trk_tb_p,target) != 0)
        goto out;

    // Update parent
    plv2->attributes.s.attrs.children++;
    plv2->attributes.s.attrs.mtime = plv2->attributes.s.attrs.ctime = time(NULL);

    // Write attributes of parents
    if (export_lv2_write_attributes(e->trk_tb_p,plv2) != 0)
        goto out;

    // Return attributes
    memcpy(attrs, &target->attributes, sizeof (mattr_t));
    /*
    ** return the parent attributes
    */
    memcpy(pattrs, &plv2->attributes.s.attrs, sizeof (mattr_t));
    status = 0;

out:
    /*
    ** check if parent root idx bitmap must be updated
    */
    if (plv2 != NULL) export_dir_flush_root_idx_bitmap(e,newparent,plv2->dirent_root_idx_p);
    
    if (fdp != -1) close(fdp);
    STOP_PROFILING(export_link);
    return status;
}
/*
**__________________________________________________________________
*/
/** create a new file
 *
 * @param e: the export managing the file
 * @param site_number: site number for geo-replication
 * @param pfid: the id of the parent
 * @param name: the name of this file.
 * @param uid: the user id
 * @param gid: the group id
 * @param mode: mode of this file
 * @param[out] attrs: mattr_t to fill (child attributes used by upper level functions)
 * @param[out] pattrs: mattr_t to fill (parent attributes)
  
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_mknod(export_t *e,uint32_t site_number,fid_t pfid, char *name, uint32_t uid,
        uint32_t gid, mode_t mode, mattr_t *attrs,mattr_t *pattrs) {
    int status = -1;
    lv2_entry_t *plv2=NULL;
    fid_t node_fid;
    int xerrno = errno;
    uint32_t type;
    int fdp= -1;
    ext_mattr_t ext_attrs;
    uint32_t pslice;
    int inode_allocated = 0;

    START_PROFILING(export_mknod);

    // get the lv2 parent
    if (!(plv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, pfid)))
        goto error;
    /*
    ** load the root_idx bitmap of the parent
    */
    export_dir_load_root_idx_bitmap(e,pfid,plv2);
    
    // check if exists
    fdp = export_open_parent_directory(e,pfid);
    if (fdp == -1) 
       goto out;
    if (get_mdirentry(plv2->dirent_root_idx_p,fdp, pfid, name, node_fid, &type) == 0) {
        errno = EEXIST;
        goto error;
    }
    /*
     ** nothing has been found, need to check the read only flag:
     ** that flag is asserted if some parts of dirent files are unreadable 
     */
    if (DIRENT_ROOT_IS_READ_ONLY()) {
        xerrno = EIO;
        goto error_read_only;
    }

    if (!S_ISREG(mode)) {
        errno = ENOTSUP;
        goto error;
    }    
    /*
    ** Check that some space os left for the new file in case a hard quota is set
    */
    if (e->hquota) {
      if (e->fstat.blocks >= e->hquota) {
        errno = ENOSPC;
       goto error;
      }
    }
    /*
    ** get the slice of the parent
    */
    exp_trck_get_slice(pfid,&pslice);
    /*
    ** copy the parent fid and the name of the regular file
    */
    memset(&ext_attrs,0x00,sizeof(ext_attrs));
    memcpy(&ext_attrs.s.pfid,pfid,sizeof(fid_t));
    strcpy(&ext_attrs.s.name[0],name);

    /*
    ** get the distribution for the file
    */
    if (volume_distribute(e->volume,site_number, &ext_attrs.s.attrs.cid, ext_attrs.s.attrs.sids) != 0)
        goto error;
    ext_attrs.s.attrs.mode = mode;
    ext_attrs.s.attrs.uid = uid;
    ext_attrs.s.attrs.gid = gid;
    ext_attrs.s.attrs.nlink = 1;
    ext_attrs.s.i_extra_isize = ROZOFS_I_EXTRA_ISIZE;
    ext_attrs.s.i_state = 0;
    ext_attrs.s.i_file_acl = 0;
    ext_attrs.s.i_link_name = 0;

   /*
   ** set atime,ctime and mtime
   */
    if ((ext_attrs.s.attrs.ctime = ext_attrs.s.attrs.atime = ext_attrs.s.attrs.mtime = time(NULL)) == -1)
        goto error;
    ext_attrs.s.attrs.size = 0;
    /*
    ** create the inode and write the attributes on disk
    */
    if(exp_attr_create(e->trk_tb_p,pslice,&ext_attrs,ROZOFS_REG,NULL) < 0)
        goto error;
    inode_allocated = 1;
/*
    {
         rozofs_inode_t *fake_inode = (rozofs_inode_t*)ext_attrs.s.attrs.fid;
	 severe("FDL name %d inode %llx-slice %d- file_id %llu -idx %d -key %d",name,
	         fake_inode->s.fid_high,
	         fake_inode->s.usr_id,
	         fake_inode->s.file_id,
	         fake_inode->s.idx,
	         fake_inode->s.key);    
    }
*/
    /*
    ** update the bit in the root_idx bitmap of the parent directory
    */
    uint32_t hash1,hash2;
    int root_idx;
    int len;
    
    hash1 = filename_uuid_hash_fnv(0, name,pfid, &hash2, &len);
    root_idx = hash1 & DIRENT_MASK_FOR_EXPORT;
    export_dir_update_root_idx_bitmap(plv2->dirent_root_idx_p,root_idx,1);
    if (export_dir_flush_root_idx_bitmap(e,pfid,plv2->dirent_root_idx_p) < 0)
    {
       errno = EPROTO; 
       goto error;
    }
    // update the parent
    // add the new child to the parent
    if (put_mdirentry(plv2->dirent_root_idx_p,fdp, pfid, name, ext_attrs.s.attrs.fid, attrs->mode) != 0) {
        goto error;
    }

    // Update children nb. and times of parent
    plv2->attributes.s.attrs.children++;
    plv2->attributes.s.attrs.mtime = plv2->attributes.s.attrs.ctime = time(NULL);
    if (export_lv2_write_attributes(e->trk_tb_p,plv2) != 0) {
        goto error;
    }

    // update export files
    if (export_update_files(e, 1) != 0)
        goto error;

    status = 0;
    /*
    ** return the parent attributes and the child attributes
    */
    memcpy(pattrs, &plv2->attributes.s.attrs, sizeof (mattr_t));
    memcpy(attrs, &ext_attrs.s.attrs, sizeof (mattr_t));
    goto out;

error:
    xerrno = errno;
    if (inode_allocated)
    {
       export_tracking_table_t *trk_tb_p;
   
        trk_tb_p = e->trk_tb_p;
        exp_attr_delete(trk_tb_p,ext_attrs.s.attrs.fid);        
    }
error_read_only:
    errno = xerrno;

out:
    /*
    ** check if parent root idx bitmap must be updated
    */
    if (plv2 != NULL) export_dir_flush_root_idx_bitmap(e,pfid,plv2->dirent_root_idx_p);
    
    if(fdp != -1) close(fdp);
    STOP_PROFILING(export_mknod);
    return status;
}
/*
**__________________________________________________________________
*/
/** create a new directory
 *
 * @param e: the export managing the file
 * @param pfid: the id of the parent
 * @param name: the name of this file.
 * @param uid: the user id
 * @param gid: the group id
 * @param mode: mode of this file
 * @param[out] attrs: mattr_t to fill (child attributes used by upper level functions)
 * @param[out] pattrs: mattr_t to fill (parent attributes)
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_mkdir(export_t *e, fid_t pfid, char *name, uint32_t uid,
        uint32_t gid, mode_t mode, mattr_t * attrs,mattr_t * pattrs) {
    int status = -1;
    lv2_entry_t *plv2= NULL;
    char node_path[PATH_MAX];
    mdir_t node_mdir;
    int xerrno = errno;
    int fdp = -1;
    ext_mattr_t ext_attrs;
    uint32_t pslice;
    fid_t node_fid;
    int inode_allocated = 0;
    dirent_dir_root_idx_bitmap_t root_idx_bitmap;
    dirent_dir_root_idx_bitmap_t *root_idx_bitmap_p = NULL;

   
    START_PROFILING(export_mkdir);

    
    // get the lv2 parent
    if (!(plv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, pfid)))
        goto error;
    /*
    ** load the root_idx bitmap of the old parent
    */
    export_dir_load_root_idx_bitmap(e,pfid,plv2);

    // check if exists
    fdp = export_open_parent_directory(e,pfid);
    if (fdp == -1) 
       goto out;
    if (get_mdirentry(plv2->dirent_root_idx_p,fdp, pfid, name, node_fid, &attrs->mode) == 0) {
        errno = EEXIST;
        goto error;
    }
    /*
     ** nothing has been found, need to check the read only flag:
     ** that flag is asserted if some parts of dirent files are unreadable 
     */
    if (DIRENT_ROOT_IS_READ_ONLY()) {
        xerrno = EIO;
        goto error_read_only;
    }    
    /*
    ** Check that some space is left for the new file in case a hard quota is set
    */
    if (e->hquota) {
      if (e->fstat.blocks >= e->hquota) {
        errno = ENOSPC;
       goto error;
      }
    }    
    /*
    ** get the slice of the parent
    */
    exp_trck_get_slice(pfid,&pslice);
    /*
    ** copy the parent fid and the name of the regular file
    */
    memcpy(&ext_attrs.s.pfid,pfid,sizeof(fid_t));
    memset(&ext_attrs.s.name[0],0,ROZOFS_MAXATTR);
    strncpy(&ext_attrs.s.name[0],name,ROZOFS_MAXATTR-1);
    attrs->cid = 0;
    memset(&ext_attrs.s.attrs.sids, 0, ROZOFS_SAFE_MAX * sizeof (sid_t));
    ext_attrs.s.i_extra_isize = ROZOFS_I_EXTRA_ISIZE;
    ext_attrs.s.i_state = 0;
    ext_attrs.s.i_file_acl = 0;
    ext_attrs.s.i_link_name = 0;
    ext_attrs.s.attrs.mode = mode;
    ext_attrs.s.attrs.uid = uid;
    ext_attrs.s.attrs.gid = gid;
    ext_attrs.s.attrs.nlink = 2;
    if ((ext_attrs.s.attrs.ctime = ext_attrs.s.attrs.atime = ext_attrs.s.attrs.mtime = time(NULL)) == -1)
        goto error;
    ext_attrs.s.attrs.size = ROZOFS_DIR_SIZE;
    ext_attrs.s.attrs.children = 0;
    /*
    ** create the inode and write the attributes on disk
    */
    if(exp_attr_create(e->trk_tb_p,pslice,&ext_attrs,ROZOFS_DIR,NULL) < 0)
        goto error;
    /*
    ** indicates that the inode has been allocated: needed in case of error to release it
    */
    inode_allocated = 1;
    // create the lv2 directory
    if (export_lv2_resolve_path(e, ext_attrs.s.attrs.fid, node_path) != 0)
        goto error;

    if (mkdir(node_path, S_IRWXU) != 0)
        goto error;
	    
    // write attributes to mdir file
    if (mdir_open(&node_mdir, node_path) < 0)
        goto error;
    /**
    * clear the root idx bitmap before creating . and ..
    * the bitmap is written on disk before creation the dirent since if we do
    * it at the end, in case of failure the object could not be listed by list_direntry
    */
    uint32_t hash1,hash2;
    int root_idx;
    int len;
    memset(&root_idx_bitmap,0,sizeof(dirent_dir_root_idx_bitmap_t));
    
    hash1 = filename_uuid_hash_fnv(0, ".", ext_attrs.s.attrs.fid, &hash2, &len);
    root_idx = hash1 & DIRENT_MASK_FOR_EXPORT;
    export_dir_update_root_idx_bitmap(&root_idx_bitmap,root_idx,1);
    hash1 = filename_uuid_hash_fnv(0, "..", ext_attrs.s.attrs.fid, &hash2, &len);
    root_idx = hash1 & DIRENT_MASK_FOR_EXPORT;
    export_dir_update_root_idx_bitmap(&root_idx_bitmap,root_idx,1);
    if (export_dir_flush_root_idx_bitmap(e,ext_attrs.s.attrs.fid,&root_idx_bitmap) < 0)
    {
       errno = EPROTO; 
       goto error;
    }
    // create "." ".." lv3 entries
    if (put_mdirentry(&root_idx_bitmap,node_mdir.fdp, ext_attrs.s.attrs.fid, ".", ext_attrs.s.attrs.fid, S_IFDIR | S_IRWXU) != 0) {
        mdir_close(&node_mdir);
        return -1;
    }
    if (put_mdirentry(&root_idx_bitmap,node_mdir.fdp, ext_attrs.s.attrs.fid, "..", plv2->attributes.s.attrs.fid, S_IFDIR | S_IRWXU) != 0) {
        mdir_close(&node_mdir);
        return -1;
    }

    hash1 = filename_uuid_hash_fnv(0, name, pfid, &hash2, &len);
    root_idx = hash1 & DIRENT_MASK_FOR_EXPORT;
    export_dir_update_root_idx_bitmap(plv2->dirent_root_idx_p,root_idx,1);
    if (export_dir_flush_root_idx_bitmap(e,pfid,plv2->dirent_root_idx_p) < 0)
    {
       errno = EPROTO; 
       goto error;
    }
    // update the parent
    // add the new child to the parent
    if (put_mdirentry(plv2->dirent_root_idx_p,fdp, pfid, name, ext_attrs.s.attrs.fid, attrs->mode) != 0) {
        goto error;
    }

    plv2->attributes.s.attrs.children++;
    plv2->attributes.s.attrs.nlink++;
    plv2->attributes.s.attrs.mtime = plv2->attributes.s.attrs.ctime = time(NULL);
    if (export_lv2_write_attributes(e->trk_tb_p,plv2) != 0)
        goto error;

    // update export files
    if (export_update_files(e, 1) != 0)
        goto error;

    mdir_close(&node_mdir);
    status = 0;
    /*
    ** return the parent and child attributes
    */
    memcpy(attrs, &ext_attrs.s.attrs, sizeof (mattr_t));
    memcpy(pattrs, &plv2->attributes.s.attrs, sizeof (mattr_t));
    /*
    ** indicates that the root bitmap of the new directory must be updated
    */
    root_idx_bitmap_p = &root_idx_bitmap;
    goto out;

error:
    xerrno = errno;
    if (inode_allocated)
    {
       export_tracking_table_t *trk_tb_p;
   
        trk_tb_p = e->trk_tb_p;
        exp_attr_delete(trk_tb_p,ext_attrs.s.attrs.fid);        
    }
    if (xerrno != EEXIST) {
        char fname[PATH_MAX];
        mdir_t node_mdir_del;
        // XXX: put version
        fid_t fid;
        uint32_t type;
        sprintf(fname, "%s/%s", node_path, MDIR_ATTRS_FNAME);
        unlink(fname);
        node_mdir_del.fdp = open(node_path, O_RDONLY, S_IRWXU);
        del_mdirentry(NULL,node_mdir_del.fdp, ext_attrs.s.attrs.fid, ".", fid, &type);
        del_mdirentry(NULL,node_mdir_del.fdp, ext_attrs.s.attrs.fid, "..", fid, &type);
        rmdir(node_path);
        mdir_close(&node_mdir_del);
    }
error_read_only:
    errno = xerrno;

out:
    /*
    ** check if parent root idx bitmap must be updated
    */
    if (plv2 != NULL) export_dir_flush_root_idx_bitmap(e,pfid,plv2->dirent_root_idx_p);
    /*
    ** check if  root idx bitmap must be updated for the new directory
    */
    if (root_idx_bitmap_p != NULL) export_dir_flush_root_idx_bitmap(e,ext_attrs.s.attrs.fid,root_idx_bitmap_p);
    
    if(fdp != -1) close(fdp);
    STOP_PROFILING(export_mkdir);
    return status;
}
/*
**__________________________________________________________________
*/
/**
*  remove the metadata associated with a file
   this corresponds to:
     the removing of extra block used for extended attributes
     the remove of the block associated with a symbolic link
   
   e : pointer to the exportd associated with the file
   lvl2: entry associated with the file
*/
int exp_delete_file(export_t * e, lv2_entry_t *lvl2)
{
   rozofs_inode_t fake_inode; 
   export_tracking_table_t *trk_tb_p;
   fid_t fid;     
   
    trk_tb_p = e->trk_tb_p;
    /*
    ** get the pointer to the attributes of the file
    */
    ext_mattr_t *rozofs_attr_p;
    
    rozofs_attr_p = &lvl2->attributes;
    /*
    ** check the presence of the extended attributes block
    */
    if (rozofs_attr_p->s.i_file_acl)
    {
       /*
       ** release the block used for extended attributes
       */
       fake_inode.fid[1] = rozofs_attr_p->s.i_file_acl;
       memcpy(fid,&fake_inode.fid[0],sizeof(fid_t));
       exp_attr_delete(trk_tb_p,fid);    
    }
    if (rozofs_attr_p->s.i_link_name)
    {
       /*
       ** release the block used for symbolic link
       */
       fake_inode.fid[1] = rozofs_attr_p->s.i_link_name;
       memcpy(fid,&fake_inode.fid[0],sizeof(fid_t));
       exp_attr_delete(trk_tb_p,fid);        
    }
    /*
    ** now delete the inode that contains the main attributes
    */
    exp_attr_delete(trk_tb_p,rozofs_attr_p->s.attrs.fid);
    return 0;        
}  
/*
**__________________________________________________________________
*/
/** remove a file
 *
 * @param e: the export managing the file
 * @param pfid: the id of the parent
 * @param name: the name of this file.
 * @param[out] fid: the fid of the removed file
 * @param[out] pattrs: mattr_t to fill (parent attributes)
 * 
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_unlink(export_t * e, fid_t parent, char *name, fid_t fid,mattr_t * pattrs) {
    int status = -1;
    lv2_entry_t *plv2=NULL, *lv2=NULL;
    fid_t child_fid;
    uint32_t child_type;
    uint16_t nlink = 0;
    int fdp = -1;
    int ret;
    rozofs_inode_t *fake_inode_p;
    rmfentry_disk_t trash_entry;

    START_PROFILING(export_unlink);

    // Get the lv2 parent
    if (!(plv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, parent)))
        goto out;
    /*
    ** load the root_idx bitmap of the old parent
    */
    export_dir_load_root_idx_bitmap(e,parent,plv2);

    // Check if name exist
    fdp = export_open_parent_directory(e,parent);
    if (fdp == -1) 
       goto out;
    if (get_mdirentry(plv2->dirent_root_idx_p,fdp, parent, name, child_fid, &child_type) != 0)
        goto out;

    if (S_ISDIR(child_type)) {
        errno = EISDIR;
        goto out;
    }

    // Delete the mdirentry if exist
    if (del_mdirentry(plv2->dirent_root_idx_p,fdp, parent, name, child_fid, &child_type) != 0)
        goto out;

    // Get mattrs of child to delete
    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, child_fid)))
        goto out;

    // Return the fid of deleted file
    memcpy(fid, child_fid, sizeof (fid_t));


    // Get nlink
    nlink = lv2->attributes.s.attrs.nlink;

    // 2 cases:
    // nlink > 1, it's a hardlink -> not delete the lv2 file
    // nlink=1, it's not a harlink -> put the lv2 file on trash directory

    // Not a hardlink
    if (nlink == 1) {

        if (lv2->attributes.s.attrs.size > 0 && S_ISREG(lv2->attributes.s.attrs.mode)) {

            // Compute hash value for this fid
            uint32_t hash = 0;
            uint8_t *c = 0;
            for (c = lv2->attributes.s.attrs.fid; c != lv2->attributes.s.attrs.fid + 16; c++)
                hash = *c + (hash << 6) + (hash << 16) - hash;
            hash %= RM_MAX_BUCKETS;
	    
            /*
	    ** prepare the trash entry
	    */
	    trash_entry.size = lv2->attributes.s.attrs.size;
            memcpy(trash_entry.fid, lv2->attributes.s.attrs.fid, sizeof (fid_t));
            trash_entry.cid = lv2->attributes.s.attrs.cid;
            memcpy(trash_entry.initial_dist_set, lv2->attributes.s.attrs.sids,
                    sizeof (sid_t) * ROZOFS_SAFE_MAX);
            memcpy(trash_entry.current_dist_set, lv2->attributes.s.attrs.sids,
                    sizeof (sid_t) * ROZOFS_SAFE_MAX);
	    fake_inode_p =  (rozofs_inode_t *)parent;   
            ret = exp_trash_entry_create(e->trk_tb_p,fake_inode_p->s.usr_id,&trash_entry); 
	    if (ret < 0)
	    {
	       /*
	       ** error while inserting entry in trash file
	       */
	       severe("error on trash insertion name %s error %s",name,strerror(errno)); 
            }
            /*
	    ** delete the metadata associated with the file
	    */
	    ret = exp_delete_file(e,lv2);
	    /*
	    * In case of geo replication, insert a delete request from the 2 sites 
	    */
	    if (e->volume->georep) 
	    {
	      /*
	      ** update the geo replication: set start=end=0 to indicate a deletion 
	      */
	      geo_rep_insert_fid(e->geo_replication_tb[0],
                		 lv2->attributes.s.attrs.fid,
				 0/*start*/,0/*end*/,
				 e->layout,
				 lv2->attributes.s.attrs.cid,
				 lv2->attributes.s.attrs.sids);
	      /*
	      ** update the geo replication: set start=end=0 to indicate a deletion 
	      */
	      geo_rep_insert_fid(e->geo_replication_tb[1],
                		 lv2->attributes.s.attrs.fid,
				 0/*start*/,0/*end*/,
				 e->layout,
				 lv2->attributes.s.attrs.cid,
				 lv2->attributes.s.attrs.sids);
	    }	
            /*
	    ** Preparation of the rmfentry
	    */
            rmfentry_t *rmfe = xmalloc(sizeof (rmfentry_t));
            memcpy(rmfe->fid, trash_entry.fid, sizeof (fid_t));
            rmfe->cid = trash_entry.cid;
            memcpy(rmfe->initial_dist_set, trash_entry.initial_dist_set,
                    sizeof (sid_t) * ROZOFS_SAFE_MAX);
            memcpy(rmfe->current_dist_set, trash_entry.current_dist_set,
                    sizeof (sid_t) * ROZOFS_SAFE_MAX);
            memcpy(rmfe->trash_inode,trash_entry.trash_inode,sizeof(fid_t));
            list_init(&rmfe->list);
            /* Acquire lock on bucket trash list
	    */
            if ((errno = pthread_rwlock_wrlock
                    (&e->trash_buckets[hash].rm_lock)) != 0) {
                severe("pthread_rwlock_wrlock failed: %s", strerror(errno));
                // Best effort
            }
            /*
	    ** Check size of file 
	    */
            if (lv2->attributes.s.attrs.size >= RM_FILE_SIZE_TRESHOLD) {
                // Add to front of list
                list_push_front(&e->trash_buckets[hash].rmfiles, &rmfe->list);
            } else {
                // Add to back of list
                list_push_back(&e->trash_buckets[hash].rmfiles, &rmfe->list);
            }

            if ((errno = pthread_rwlock_unlock
                    (&e->trash_buckets[hash].rm_lock)) != 0) {
                severe("pthread_rwlock_unlock failed: %s", strerror(errno));
                // Best effort
            }
            // Update the nb. of blocks
            if (export_update_blocks(e,
                    -(((int64_t) lv2->attributes.s.attrs.size + ROZOFS_BSIZE_BYTES(e->bsize) - 1)
                    / ROZOFS_BSIZE_BYTES(e->bsize))) != 0) {
                severe("export_update_blocks failed: %s", strerror(errno));
                // Best effort
            }
        } else {
	    /*
	    ** release the inode entry
	    */
	    if (exp_delete_file(e,lv2) < 0)
	    {
	       severe("error on inode %s release : %s",name,strerror(errno));
	    }
        }
       // Update export files
        if (export_update_files(e, -1) != 0)
            goto out;

        // Remove from the cache (will be closed and freed)
        lv2_cache_del(e->lv2_cache, child_fid);
    }
    // It's a hardlink
    if (nlink > 1) {
        lv2->attributes.s.attrs.nlink--;
        lv2->attributes.s.attrs.ctime = time(NULL);
        export_lv2_write_attributes(e->trk_tb_p,lv2);
        // Return a empty fid because no inode has been deleted
        //memset(fid, 0, sizeof (fid_t));
    }

    // Update parent
    plv2->attributes.s.attrs.mtime = plv2->attributes.s.attrs.ctime = time(NULL);
    plv2->attributes.s.attrs.children--;

    // Write attributes of parents
    if (export_lv2_write_attributes(e->trk_tb_p,plv2) != 0)
        goto out;
    /*
    ** return the parent attributes
    */
    memcpy(pattrs, &plv2->attributes.s.attrs, sizeof (mattr_t));
    status = 0;

out:
    /*
    ** check if parent root idx bitmap must be updated
    */
    if (plv2 != NULL) export_dir_flush_root_idx_bitmap(e,parent,plv2->dirent_root_idx_p);
    
    if(fdp != -1) close(fdp);
    STOP_PROFILING(export_unlink);
    return status;
}
/*
**______________________________________________________________________________
*/
static int init_storages_cnx(volume_t *volume, list_t *list) {
    list_t *p, *q;
    int status = -1;
    DEBUG_FUNCTION;
    int i;
    
    if ((errno = pthread_rwlock_rdlock(&volume->lock)) != 0) {
        severe("pthread_rwlock_rdlock failed (vid: %d): %s", volume->vid,
                strerror(errno));
        goto out;
    }

    list_for_each_forward(p, &volume->clusters) {

        cluster_t *cluster = list_entry(p, cluster_t, list);
        for (i = 0; i < ROZOFS_GEOREP_MAX_SITE;i++) {
          list_for_each_forward(q, (&cluster->storages[i])) {

              volume_storage_t *vs = list_entry(q, volume_storage_t, list);

              mclient_t * mclt = (mclient_t *) xmalloc(sizeof (mclient_t));

              strncpy(mclt->host, vs->host, ROZOFS_HOSTNAME_MAX);
              mclt->cid = cluster->cid;
              mclt->sid = vs->sid;
              struct timeval timeo;
              timeo.tv_sec = ROZOFS_MPROTO_TIMEOUT_SEC;
              timeo.tv_usec = 0;

	      init_rpcctl_ctx(&mclt->rpcclt);

              if (mclient_initialize(mclt, timeo) != 0) {
                  warning("failed to join: %s,  %s", vs->host, strerror(errno));
              }

              cnxentry_t *cnx_entry = (cnxentry_t *) xmalloc(sizeof (cnxentry_t));
              cnx_entry->cnx = mclt;

              // Add to the list
              list_push_back(list, &cnx_entry->list);

          }
	}
    }

    if ((errno = pthread_rwlock_unlock(&volume->lock)) != 0) {
        severe("pthread_rwlock_unlock failed (vid: %d): %s", volume->vid,
                strerror(errno));
        goto out;
    }

    status = 0;
out:

    return status;
}
/*
**______________________________________________________________________________
*/
static mclient_t * lookup_cnx(list_t *list, cid_t cid, sid_t sid) {

    list_t *p;
    DEBUG_FUNCTION;

    list_for_each_forward(p, list) {
        cnxentry_t *cnx_entry = list_entry(p, cnxentry_t, list);

        if ((sid == cnx_entry->cnx->sid) && (cid == cnx_entry->cnx->cid)) {
            return cnx_entry->cnx;
            break;
        }
    }

    severe("lookup_cnx failed: storage connexion (cid: %u; sid: %u) not found",
            cid, sid);

    errno = EINVAL;

    return NULL;
}
/*
**______________________________________________________________________________
*/
static void release_storages_cnx(list_t *list) {

    list_t *p, *q;
    DEBUG_FUNCTION;

    list_for_each_forward_safe(p, q, list) {

        cnxentry_t *cnx_entry = list_entry(p, cnxentry_t, list);
        mclient_release(cnx_entry->cnx);
        if (cnx_entry->cnx != NULL)
            free(cnx_entry->cnx);
        list_remove(p);
        if (cnx_entry != NULL)
            free(cnx_entry);
    }
}

/*
**_______________________________________________________________________________
*/
/**
* remove the file from the tracking file associated with the trash

   top_hdr: pointer to the top header for all slices for a given object type
   usr_id : slice to consider
   rmfentry_t *entry: pointer to the entry to remove
   
*/   
int export_rmbins_remove_from_tracking_file(export_t * e,rmfentry_t *entry)
{

   rozofs_inode_t *fake_inode;
   int nb_entries;
   int ret;
   exp_trck_file_header_t tracking_buffer_src;
   int current_count;
   exp_trck_header_memory_t *slice_hdr_p;
   char pathname[1024];
   exp_trck_top_header_t *trash_p = e->trk_tb_p->tracking_table[ROZOFS_TRASH];
   
   fake_inode = (rozofs_inode_t*)&entry->trash_inode;
   
   slice_hdr_p = trash_p->entry_p[fake_inode->s.usr_id];
   
   ret = exp_attr_delete(e->trk_tb_p,entry->trash_inode);
   if (ret < 0)
   {
      severe("error while remove file from trash tacjing file ; %s",strerror(errno));   
   }
   /*
   ** check if the tracking file must be release
   */
   ret = exp_metadata_get_tracking_file_header(trash_p,fake_inode->s.usr_id,fake_inode->s.file_id,&tracking_buffer_src,&nb_entries);
   if (ret < 0)
   {
     if (errno != ENOENT)
     {
        printf("error while reading metadata header %s\n",strerror(errno));
       // exit(-1);
       return 0;
     }
     /*
     ** nothing the delete there, so continue with the next one
     */
     return 0;
   }
   /*
   ** get the current count of file to delete
   */
   current_count= exp_metadata_get_tracking_file_count(&tracking_buffer_src);
   /*
   ** if the current count is 0 and if the current file does not correspond to the last main index
   ** the file is deleted
   */
   if ((current_count == 0) && (nb_entries == EXP_TRCK_MAX_INODE_PER_FILE))
   {
      /*
      ** delete trashing file and update the first index if that one was the first
      */
      sprintf(pathname,"%s/%d/trk_%llu",trash_p->root_path,fake_inode->s.usr_id,(long long unsigned int)fake_inode->s.file_id);
      ret = unlink(pathname);
      if (ret < 0)
      {
	severe("cannot delete %s:%s\n",pathname,strerror(errno));
      }
      /*
      ** check if the file tracking correspond to the first index of the main tracking file
      */
      if (slice_hdr_p->entry.first_idx == fake_inode->s.file_id)
      {
	/*
	** update the main tracking file
	*/
	slice_hdr_p->entry.first_idx++;
	exp_trck_write_main_tracking_file(trash_p->root_path,fake_inode->s.usr_id,0,sizeof(uint64_t),&slice_hdr_p->entry.first_idx);
      }
   }
   return 0;
}

/*
**_______________________________________________________________________________
*/

int export_rm_bins(export_t * e, uint16_t * first_bucket_idx) {
    int status = -1;
    int rm_bins_file_nb = 0;
    int i = 0;
    uint16_t idx = 0;
    uint16_t bucket_idx = 0;
    uint8_t cnx_init = 0;
    int limit_rm_files = RM_FILES_MAX;
    int curr_rm_files = 0;
    uint8_t rozofs_safe = 0;
    list_t connexions;

    DEBUG_FUNCTION;

    // Get the nb. of safe storages for this layout
    rozofs_safe = rozofs_get_rozofs_safe(e->layout);

    // For each trash slice (MAX_SLICE_NB slices)
    // Begin with trash bucket idx = *first_bucket_idx
    for (idx = *first_bucket_idx; idx < (*first_bucket_idx + RM_MAX_BUCKETS);idx++) 
    {

        /*
	** compute the slice to check
	*/
        bucket_idx = idx % RM_MAX_BUCKETS;

        // Check if the bucket is empty
        if (list_empty(&e->trash_buckets[bucket_idx].rmfiles))
            continue; // Try with the next bucket

        // If the connexions are not initialized
        if (cnx_init == 0) {
            // Init list of connexions
            list_init(&connexions);
            cnx_init = 1;
            if (init_storages_cnx(e->volume, &connexions) != 0) {
                // Problem with lock
                severe("init_storages_cnx failed: %s", strerror(errno));
                goto out;
            }
        }

        // Acquire lock on this list
        if ((errno = pthread_rwlock_wrlock
                (&e->trash_buckets[bucket_idx].rm_lock)) != 0) {
            severe("pthread_rwlock_wrlock failed: %s", strerror(errno));
            continue; // Best effort
        }

        // Remove rmfentry_t from the list of files to remove for this bucket
        rmfentry_t *entry = list_first_entry(&e->trash_buckets[bucket_idx].rmfiles, rmfentry_t, list);
        list_remove(&entry->list);

        if ((errno = pthread_rwlock_unlock(&e->trash_buckets[bucket_idx].rm_lock)) != 0) {
            severe("pthread_rwlock_unlock failed: %s", strerror(errno));
            continue;
        }

        // Nb. of bins files removed for this file
        rm_bins_file_nb = 0;

        // For each storage associated with this file
        for (i = 0; i < rozofs_safe; i++) {

            mclient_t* stor = NULL;

            if (0 == entry->current_dist_set[i]) {
                // The bins file has already been deleted for this server
                rm_bins_file_nb++;
                continue; // Go to the next storage
            }

            if ((stor = lookup_cnx(&connexions, entry->cid,
                    entry->current_dist_set[i])) == NULL) {
                // lookup_cnx failed !!! 
                continue; // Go to the next storage
            }

            if (0 == stor->status) {
                // This storage is down
                // it's not necessary to send a request
                continue; // Go to the next storage
            }

            // Send remove request
            if (mclient_remove(stor, entry->fid) != 0) {
                // Problem with request
                warning("mclient_remove failed (cid: %u; sid: %u): %s",
                        stor->cid, stor->sid, strerror(errno));
                continue; // Go to the next storage
            }

            // The bins file has been deleted successfully
            // Update distribution and nb. of bins file deleted
            entry->current_dist_set[i] = 0;
            rm_bins_file_nb++;
        }

        // If all bins files are deleted
        // Remove the file from trash
        if (rm_bins_file_nb == rozofs_safe) 
	{
	    /*
	    ** remove the entry from the trash file
	    */
	    export_rmbins_remove_from_tracking_file(e,entry);
            /*
	    **  Free entry
	    */
            if (entry != NULL)
                free(entry);

        } else { // If NO all bins are deleted

            if ((errno = pthread_rwlock_wrlock
                    (&e->trash_buckets[bucket_idx].rm_lock)) != 0) {
                severe("pthread_rwlock_wrlock failed: %s", strerror(errno));
                continue; // Best effort
            }

            // Repush back entry in the list of files to delete
            list_push_back(&e->trash_buckets[bucket_idx].rmfiles, &entry->list);

            if ((errno = pthread_rwlock_unlock
                    (&e->trash_buckets[bucket_idx].rm_lock)) != 0) {
                severe("pthread_rwlock_unlock failed: %s", strerror(errno));
            }
        }
        // Update the nb. of files that have been tested to be deleted.
        curr_rm_files++;

        // Check if enough files are removed
        if (curr_rm_files >= limit_rm_files)
            break; // Exit from the loop
    }

    // Update the first bucket index to use for the next call
    if (0 == curr_rm_files) {
        // If no files removed 
        // The next first bucket index will be 0
        // not necessary but better for debug
        *first_bucket_idx = 0;
    } else {
        *first_bucket_idx = (bucket_idx + 1) % RM_MAX_BUCKETS;
    }

    status = 0;
out:
    if (cnx_init == 1) {
        // Release storage connexions
        release_storages_cnx(&connexions);
    }
    return status;
}

/*
**______________________________________________________________________________
*/
/**
*   exportd rmdir: delete a directory

    @param pfid : fid of the parent and directory  name 
    @param name : fid of the parent and directory  name 
    
    @param[out] fid:  fid of the deleted directory 
    @param[out] pattrs:  attributes of the parent 
    
    @retval: 0 : success
    @retval: <0 error see errno
*/
int export_rmdir(export_t *e, fid_t pfid, char *name, fid_t fid,mattr_t * pattrs) {
    int status = -1;
    lv2_entry_t *plv2=NULL;
    lv2_entry_t *lv2=NULL;
    fid_t fake_fid;
    fid_t dot_fid;
    fid_t dot_dot_fid;
    uint32_t fake_type;
    uint32_t dot_type;
    uint32_t dot_dot_type;
    char lv2_path[PATH_MAX];
    char lv3_path[PATH_MAX];
    int fdp = -1;
    int fdc = -1;
    
    START_PROFILING(export_rmdir);

    // get the lv2 parent
    if (!(plv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, pfid)))
        goto out;
    /*
    ** load the root_idx bitmap of the  parent
    */
    export_dir_load_root_idx_bitmap(e,pfid,plv2);

    // get the fid according to name
    fdp = export_open_parent_directory(e,pfid);
    if (fdp == -1) goto out;
    if (get_mdirentry(plv2->dirent_root_idx_p,fdp, pfid, name, fid, &fake_type) != 0)
        goto out;

    // get the lv2
    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid)))
        goto out;

    // sanity checks (is a directory and lv3 is empty)
    if (!S_ISDIR(lv2->attributes.s.attrs.mode)) {
        errno = ENOTDIR;
        goto out;
    }

    if (lv2->attributes.s.attrs.children != 0) {
        errno = ENOTEMPTY;
        goto out;
    }

    // remove lv2
    if (export_lv2_resolve_path(e, fid, lv2_path) != 0)
        goto out;
    /*
     ** once the attributes file has been removed 
     ** consider that the directory is deleted, all the remaining is best effort
     */
    sprintf(lv3_path, "%s/%s", lv2_path, MDIR_ATTRS_FNAME);

    if (unlink(lv3_path) != 0) {
        if (errno != ENOENT) goto out;
    }
    /*
    ** remove the '.' and '..' directory in best effort mode
    */
    fdc = export_open_parent_directory(e,fid);
    if (fdc != -1)
    {
      // XXX starting from here, any failure will leads to inconsistent state: best effort mode
      del_mdirentry(NULL,fdc, fid, ".", dot_fid, &dot_type);
      del_mdirentry(NULL,fdc, fid, "..", dot_dot_fid, &dot_dot_type);
    }
    // remove from the cache (will be closed and freed)
    lv2_cache_del(e->lv2_cache, fid);
    /*
     ** rmdir is best effort since it might possible that some dirent file with empty entries remain
     */
    rmdir(lv2_path);
    /**
    * releas the inode allocated for storing the directory attributes
    */
    if (exp_attr_delete(e->trk_tb_p,fid) < 0)
    {
       severe("error on inode %s release : %s",name,strerror(errno));
    }

    // update parent:
    /*
     ** attributes of the parent must be updated first otherwise we can afce the situation where
     ** parent directory cannot be removed because the number of children is not 0
     */
    if (plv2->attributes.s.attrs.children > 0) plv2->attributes.s.attrs.children--;
    plv2->attributes.s.attrs.nlink--;
    plv2->attributes.s.attrs.mtime = plv2->attributes.s.attrs.ctime = time(NULL);
    if (export_lv2_write_attributes(e->trk_tb_p,plv2) != 0)
        goto out;

    // update export nb files: best effort mode
    export_update_files(e, -1);

    /*
     ** remove the entry from the parent directory: best effort
     */
    del_mdirentry(plv2->dirent_root_idx_p,fdp, pfid, name, fake_fid, &fake_type);
    /*
    ** return the parent attributes
    */
    memcpy(pattrs, &plv2->attributes.s.attrs, sizeof (mattr_t));
    status = 0;
out:
    /*
    ** check if parent root idx bitmap must be updated
    */
    if (plv2 != NULL) export_dir_flush_root_idx_bitmap(e,pfid,plv2->dirent_root_idx_p);
    
    if(fdc != -1) close(fdc);
    if(fdp != -1) close(fdp);
    STOP_PROFILING(export_rmdir);

    return status;
}
/*
**______________________________________________________________________________
*/
/** create a symlink
 *
 * @param e: the export managing the file
 * @param link: target name
 * @param pfid: the id of the parent
 * @param name: the name of the file to link.
 * @param[out] attrs: mattr_t to fill (child attributes used by upper level functions)
 * @param[out] pattrs: mattr_t to fill (parent attributes)
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_symlink(export_t * e, char *link, fid_t pfid, char *name,
        mattr_t * attrs,mattr_t *pattrs) {

    int status = -1;
    lv2_entry_t *plv2=NULL;
    fid_t node_fid;
    int xerrno = errno;
    int fdp = -1;
    ext_mattr_t ext_attrs;
    uint32_t pslice;
    int inode_allocated = 0;
    
    START_PROFILING(export_symlink);

    // get the lv2 parent
    if (!(plv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, pfid)))
        goto error;
     /*
     ** load the root_idx bitmap of the parent
     */
     export_dir_load_root_idx_bitmap(e,pfid,plv2);

    // check if exists
    fdp = export_open_parent_directory(e,pfid);
    if (fdp == -1) goto out;
    if (get_mdirentry(plv2->dirent_root_idx_p,fdp, pfid, name, node_fid, &attrs->mode) == 0) {
        errno = EEXIST;
        goto error;
    }
    /*
     ** nothing has been found, need to check the read only flag:
     ** that flag is asserted if some parts of dirent files are unreadable 
     */
    if (DIRENT_ROOT_IS_READ_ONLY()) {
        xerrno = EIO;
        goto error_read_only;
    }
    /*
    ** get the slice of the parent
    */
    exp_trck_get_slice(pfid,&pslice);
    /*
    ** copy the parent fid and the name of the regular file
    */
    memset(&ext_attrs,0x00,sizeof(ext_attrs));
    memcpy(&ext_attrs.s.pfid,pfid,sizeof(fid_t));
    strcpy(&ext_attrs.s.name[0],name);

    ext_attrs.s.attrs.cid = 0;
    memset(ext_attrs.s.attrs.sids, 0, ROZOFS_SAFE_MAX * sizeof (sid_t));
    ext_attrs.s.attrs.mode = S_IFLNK | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP |
            S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH;
    ext_attrs.s.attrs.uid = getuid();
    ext_attrs.s.attrs.gid = getgid();
    ext_attrs.s.attrs.nlink = 1;
    if ((ext_attrs.s.attrs.ctime = ext_attrs.s.attrs.atime = ext_attrs.s.attrs.mtime = time(NULL)) == -1)
        goto error;
    ext_attrs.s.attrs.size = strlen(link);
    ext_attrs.s.i_extra_isize = ROZOFS_I_EXTRA_ISIZE;
    ext_attrs.s.i_state = 0;
    ext_attrs.s.i_file_acl = 0;
    ext_attrs.s.i_link_name = 0;
    /*
    ** create the inode and write the attributes on disk
    */
    if(exp_attr_create(e->trk_tb_p,pslice,&ext_attrs,ROZOFS_REG,link) < 0)
        goto error;
		
    inode_allocated = 1;
    /*
    ** update the bit in the root_idx bitmap of the parent directory
    */
    uint32_t hash1,hash2;
    int root_idx;
    int len;
    
    hash1 = filename_uuid_hash_fnv(0, name,pfid, &hash2, &len);
    root_idx = hash1 & DIRENT_MASK_FOR_EXPORT;
    export_dir_update_root_idx_bitmap(plv2->dirent_root_idx_p,root_idx,1);
    if (export_dir_flush_root_idx_bitmap(e,pfid,plv2->dirent_root_idx_p) < 0)
    {
       errno = EPROTO; 
       goto error;
    }

    // update the parent
    // add the new child to the parent
    if (put_mdirentry(plv2->dirent_root_idx_p,fdp, pfid, name, ext_attrs.s.attrs.fid, attrs->mode) != 0)
        goto error;
    plv2->attributes.s.attrs.children++;
    // update times of parent
    plv2->attributes.s.attrs.mtime = plv2->attributes.s.attrs.ctime = time(NULL);
    if (export_lv2_write_attributes(e->trk_tb_p,plv2) != 0)
        goto error;

    // update export files
    if (export_update_files(e, 1) != 0)
        goto error;

    status = 0;
    /*
    ** return the parent and child attributes
    */
    memcpy(attrs, &ext_attrs.s.attrs, sizeof (mattr_t));
    memcpy(pattrs, &plv2->attributes.s.attrs, sizeof (mattr_t));
    goto out;

error:
    xerrno = errno;
    if (inode_allocated)
    {
       export_tracking_table_t *trk_tb_p;
   
        trk_tb_p = e->trk_tb_p;
        exp_attr_delete(trk_tb_p,ext_attrs.s.attrs.fid);        
    }
error_read_only:
    errno = xerrno;

out:
    /*
    ** check if parent root idx bitmap must be updated
    */
    if (plv2 != NULL) export_dir_flush_root_idx_bitmap(e,pfid,plv2->dirent_root_idx_p);
    
    if(fdp != -1) close(fdp);
    STOP_PROFILING(export_symlink);

    return status;
}
/*
**______________________________________________________________________________
*/
/** read a symbolic link
 *
 * @param e: the export managing the file
 * @param fid: file id
 * @param link: link to fill
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_readlink(export_t *e, fid_t fid, char *link) {
    int status = -1;
    lv2_entry_t *lv2 = 0;
    export_tracking_table_t *trk_tb_p = e->trk_tb_p;
    rozofs_inode_t fake_inode;
    exp_trck_top_header_t *p = NULL;
    int ret;
     
    START_PROFILING(export_readlink);

    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid))) {
        goto out;
    }
    /**
    * read the link: the size of the link is found in the common attributes
    */
    if (lv2->attributes.s.i_link_name == 0)
    {
      errno = EINVAL;
      goto out;    
    }
    fake_inode.fid[1] = lv2->attributes.s.i_link_name;

    if (fake_inode.s.key != ROZOFS_SLNK)
    {
      errno = EINVAL;
      goto out;    
    }
    p = trk_tb_p->tracking_table[fake_inode.s.key];
    if (p == NULL)
    {
      errno = EINVAL;
      goto out;    
    }  
    /*
    ** read the link from disk
    */
    ret = exp_metadata_read_attributes(p,&fake_inode,link,lv2->attributes.s.attrs.size);
    if (ret < 0)
    { 
      goto out;    
    }
    link[lv2->attributes.s.attrs.size] = 0; 
    status = 0;

out:
    STOP_PROFILING(export_readlink);

    return status;
}
/*
**______________________________________________________________________________
*/
/** rename (move) a file
 *
 * @param e: the export managing the file
 * @param pfid: parent file id
 * @param name: file name
 * @param npfid: target parent file id
 * @param newname: target file name
 * @param fid: file id
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_rename(export_t *e, fid_t pfid, char *name, fid_t npfid,
        char *newname, fid_t fid,
	mattr_t * attrs) {
    int status = -1;
    lv2_entry_t *lv2_old_parent = 0;
    lv2_entry_t *lv2_new_parent = 0;
    lv2_entry_t *lv2_to_rename = 0;
    lv2_entry_t *lv2_to_replace = 0;
    fid_t fid_to_rename;
    uint32_t type_to_rename;
    fid_t fid_to_replace;
    uint32_t type_to_replace;
    int old_parent_fdp = -1;
    int new_parent_fdp = -1;
    int to_replace_fdp = -1;
    int to_rename_fdp  = -1;
    rmfentry_disk_t trash_entry;
    int ret;
    rozofs_inode_t *fake_inode_p;    

    START_PROFILING(export_rename);

    // Get the lv2 entry of old parent
    if (!(lv2_old_parent = export_lookup_fid(e->trk_tb_p,e->lv2_cache, pfid))) {
        goto out;
    }

    // Verify that the old parent is a directory
    if (!S_ISDIR(lv2_old_parent->attributes.s.attrs.mode)) {
        errno = ENOTDIR;
        goto out;
    }

    /*
    ** load the root_idx bitmap of the old parent
    */
    export_dir_load_root_idx_bitmap(e,pfid,lv2_old_parent);
    /*
    ** get the file descriptor associated with the old parent
    */
    old_parent_fdp = export_open_parent_directory(e,pfid);
    if (old_parent_fdp == -1) 
       goto out;

    // Check if the file/dir to rename exist
    if (get_mdirentry(lv2_old_parent->dirent_root_idx_p,old_parent_fdp, pfid, name, fid_to_rename, &type_to_rename) != 0)
        goto out;

    // Get the lv2 entry of file/dir to rename
    if (!(lv2_to_rename = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid_to_rename)))
        goto out;
    /*
    ** open the fid to rename if it is a directory
    */
    if (S_ISDIR(lv2_to_rename->attributes.s.attrs.mode)) 
    {   
      /*
      ** load the root_idx bitmap of the fid to rename
      */
      export_dir_load_root_idx_bitmap(e,fid_to_rename,lv2_to_rename);

      to_rename_fdp = export_open_parent_directory(e,fid_to_rename);
      if (to_rename_fdp == -1) 
	 goto out;
    }  
    // Get the lv2 entry of newparent
    if (!(lv2_new_parent = export_lookup_fid(e->trk_tb_p,e->lv2_cache, npfid)))
        goto out;

    // Verify that the new parent is a directory
    if (!S_ISDIR(lv2_new_parent->attributes.s.attrs.mode)) {
        errno = ENOTDIR;
        goto out;
    }
    /*
    ** load the root_idx bitmap of the old parent
    */
    export_dir_load_root_idx_bitmap(e,npfid,lv2_new_parent);

    memset(fid, 0, sizeof (fid_t));
    /*
    ** open the directory
    */
    new_parent_fdp = export_open_parent_directory(e,npfid);
    if (new_parent_fdp == -1) 
       goto out;
    // Get the old mdirentry if exist
    if (get_mdirentry(lv2_new_parent->dirent_root_idx_p,new_parent_fdp, npfid, newname, fid_to_replace, &type_to_replace) == 0) {

        // We must delete the old entry

        // Get mattrs of entry to delete
        if (!(lv2_to_replace = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid_to_replace)))
            goto out;
	/*
	** load the root_idx bitmap of the old parent
	*/
	export_dir_load_root_idx_bitmap(e,fid_to_replace,lv2_to_replace);
	
        // The entry (to replace) is an existing directory
        if (S_ISDIR(lv2_to_replace->attributes.s.attrs.mode)) {

            // The entry to rename must be a directory
            if (!S_ISDIR(lv2_to_rename->attributes.s.attrs.mode)) {
                errno = EISDIR;
                goto out;
            }

            // The entry to replace must be a empty directory
            if (lv2_to_replace->attributes.s.attrs.children != 0) {
                errno = ENOTEMPTY;
                goto out;
            }
	    /*
	    ** open the directory
	    */
	    to_replace_fdp = export_open_parent_directory(e,fid_to_replace);
	    if (to_replace_fdp == -1) 
	       goto out;
            // Delete mdirentry . and .. of dir to replace
            fid_t dot_fid, dot_dot_fid;
            uint32_t dot_type, dot_dot_type;

            if (del_mdirentry(lv2_to_replace->dirent_root_idx_p,to_replace_fdp, fid_to_replace, ".", dot_fid, &dot_type) != 0)
                goto out;
            if (del_mdirentry(lv2_to_replace->dirent_root_idx_p,to_replace_fdp, fid_to_replace, "..", dot_dot_fid, &dot_dot_type) != 0)
                goto out;

            // Update parent directory
            lv2_new_parent->attributes.s.attrs.nlink--;
            lv2_new_parent->attributes.s.attrs.children--;

            // We'll write attributes of parents after

            // Update export files
            if (export_update_files(e, -1) != 0)
                goto out;

            char lv2_path[PATH_MAX];
            char lv3_path[PATH_MAX];

            if (export_lv2_resolve_path(e, lv2_to_replace->attributes.s.attrs.fid, lv2_path) != 0)
                goto out;

            sprintf(lv3_path, "%s/%s", lv2_path, MDIR_ATTRS_FNAME);

            if (unlink(lv3_path) != 0)
                goto out;

            if (rmdir(lv2_path) != 0)
                goto out;

            // Remove the dir to replace from the cache (will be closed and freed)
            lv2_cache_del(e->lv2_cache, fid_to_replace);
	    lv2_to_replace = 0;

            // Return the fid of deleted directory
            memcpy(fid, fid_to_replace, sizeof (fid_t));

        } else {
            // The entry (to replace) is an existing file
            if (S_ISREG(lv2_to_replace->attributes.s.attrs.mode) || S_ISLNK(lv2_to_replace->attributes.s.attrs.mode)) {

                // Get nlink
                uint16_t nlink = lv2_to_replace->attributes.s.attrs.nlink;

                // 2 cases:
                // nlink > 1, it's a hardlink -> not delete the lv2 file
                // nlink=1, it's not a harlink -> put the lv2 file on trash
                // directory

                // Not a hardlink
                if (nlink == 1) {
                    // Check if it's a regular file not empty 
                    if (lv2_to_replace->attributes.s.attrs.size > 0 &&
                            S_ISREG(lv2_to_replace->attributes.s.attrs.mode)) {

                        // Compute hash value for this fid
                        uint32_t hash = 0;
                        uint8_t *c = 0;
                        for (c = lv2_to_replace->attributes.s.attrs.fid;
                                c != lv2_to_replace->attributes.s.attrs.fid + 16; c++)
                            hash = *c + (hash << 6) + (hash << 16) - hash;
                        hash %= RM_MAX_BUCKETS;
        		/*
			** prepare the trash entry
			*/
        		memcpy(trash_entry.fid, lv2_to_replace->attributes.s.attrs.fid, sizeof (fid_t));
        		trash_entry.cid = lv2_to_replace->attributes.s.attrs.cid;
        		memcpy(trash_entry.initial_dist_set, lv2_to_replace->attributes.s.attrs.sids,
                		sizeof (sid_t) * ROZOFS_SAFE_MAX);
        		memcpy(trash_entry.current_dist_set, lv2_to_replace->attributes.s.attrs.sids,
                		sizeof (sid_t) * ROZOFS_SAFE_MAX);
			fake_inode_p =  (rozofs_inode_t *)pfid;   
        		ret = exp_trash_entry_create(e->trk_tb_p,fake_inode_p->s.usr_id,&trash_entry); 
			if (ret < 0)
			{
			   /*
			   ** error while inserting entry in trash file
			   */
			   severe("error on trash insertion name %s error %s",name,strerror(errno)); 
        		}
        		/*
			** delete the metadata associated with the file
			*/
			ret = exp_delete_file(e,lv2_to_replace);						
        		/*
			** Preparation of the rmfentry
			*/
        		rmfentry_t *rmfe = xmalloc(sizeof (rmfentry_t));
        		memcpy(rmfe->fid, trash_entry.fid, sizeof (fid_t));
        		rmfe->cid = trash_entry.cid;
        		memcpy(rmfe->initial_dist_set, trash_entry.initial_dist_set,
                		sizeof (sid_t) * ROZOFS_SAFE_MAX);
        		memcpy(rmfe->current_dist_set, trash_entry.current_dist_set,
                		sizeof (sid_t) * ROZOFS_SAFE_MAX);
        		memcpy(rmfe->trash_inode,trash_entry.trash_inode,sizeof(fid_t));
                        list_init(&rmfe->list);
                        /*
                        ** Acquire lock on bucket trash list
			*/
                        if ((errno = pthread_rwlock_wrlock
                                (&e->trash_buckets[hash].rm_lock)) != 0) {
                            severe("pthread_rwlock_wrlock failed: %s",
                                    strerror(errno));
                            // Best effort
                        }
                        /*
                        ** Check size of file 
			*/
                        if (lv2_to_replace->attributes.s.attrs.size
                                >= RM_FILE_SIZE_TRESHOLD) {
                            // Add to front of list
                            list_push_front(&e->trash_buckets[hash].rmfiles,
                                    &rmfe->list);
                        } else {
                            // Add to back of list
                            list_push_back(&e->trash_buckets[hash].rmfiles,
                                    &rmfe->list);
                        }
                        /*
			** lock release
			*/
                        if ((errno = pthread_rwlock_unlock
                                (&e->trash_buckets[hash].rm_lock)) != 0) {
                            severe("pthread_rwlock_unlock failed: %s",
                                    strerror(errno));
                            // Best effort
                        }

                        // Update the nb. of blocks
                        if (export_update_blocks(e,
                                -(((int64_t) lv2_to_replace->attributes.s.attrs.size
                                + ROZOFS_BSIZE_BYTES(e->bsize) - 1) / ROZOFS_BSIZE_BYTES(e->bsize))) != 0) {
                            severe("export_update_blocks failed: %s",
                                    strerror(errno));
                            // Best effort
                        }
                    } else {
                        /* 
			** file empty: release the inode
			*/			
			if (exp_delete_file(e,lv2_to_replace) < 0)
			{
			   severe("error on inode release : %s",strerror(errno));
			}
                    }

                    // Update export files
                    if (export_update_files(e, -1) != 0)
                        goto out;

                    // Remove from the cache (will be closed and freed)
                    lv2_cache_del(e->lv2_cache, fid_to_replace);
		    lv2_to_replace = 0;

                    // Return the fid of deleted directory
                    memcpy(fid, fid_to_replace, sizeof (fid_t));
                }

                // It's a hardlink
                if (nlink > 1) {
                    lv2_to_replace->attributes.s.attrs.nlink--;
                    export_lv2_write_attributes(e->trk_tb_p,lv2_to_replace);
                    // Return a empty fid because no inode has been deleted
                    memset(fid, 0, sizeof (fid_t));
                }
                lv2_new_parent->attributes.s.attrs.children--;
            }
        }
    } else {
        /*
         ** nothing has been found, need to check the read only flag:
         ** that flag is asserted if some parts of dirent files are unreadable 
         */
        if (DIRENT_ROOT_IS_READ_ONLY()) {
            errno = EIO;
            goto out;
        }
    }

    // Put the mdirentry
    if (put_mdirentry(lv2_new_parent->dirent_root_idx_p,new_parent_fdp, npfid, newname, 
                      lv2_to_rename->attributes.s.attrs.fid,
		      lv2_to_rename->attributes.s.attrs.mode) != 0) {
        goto out;
    }

    // Delete the mdirentry
    if (del_mdirentry(lv2_old_parent->dirent_root_idx_p,
                       old_parent_fdp, 
		       pfid, name, fid_to_rename, &type_to_rename) != 0)
        goto out;

    if (memcmp(pfid, npfid, sizeof (fid_t)) != 0) {

        lv2_new_parent->attributes.s.attrs.children++;
        lv2_old_parent->attributes.s.attrs.children--;

        if (S_ISDIR(lv2_to_rename->attributes.s.attrs.mode)) {
            lv2_new_parent->attributes.s.attrs.nlink++;
            lv2_old_parent->attributes.s.attrs.nlink--;

            // If the node to rename is a directory
            // We must change the subdirectory '..'
            if (put_mdirentry(lv2_to_rename->dirent_root_idx_p,
	                      to_rename_fdp, fid_to_rename,
			       "..", lv2_new_parent->attributes.s.attrs.fid,
			       lv2_new_parent->attributes.s.attrs.mode) != 0) {
                goto out;
            }

        }

        lv2_new_parent->attributes.s.attrs.mtime = lv2_new_parent->attributes.s.attrs.ctime = time(NULL);
        lv2_old_parent->attributes.s.attrs.mtime = lv2_old_parent->attributes.s.attrs.ctime = time(NULL);

        if (export_lv2_write_attributes(e->trk_tb_p,lv2_new_parent) != 0)
            goto out;

        if (export_lv2_write_attributes(e->trk_tb_p,lv2_old_parent) != 0)
            goto out;
    } else {

        lv2_new_parent->attributes.s.attrs.mtime = lv2_new_parent->attributes.s.attrs.ctime = time(NULL);

        if (export_lv2_write_attributes(e->trk_tb_p,lv2_new_parent) != 0)
            goto out;
    }

    // Update ctime of renamed file/directory
    lv2_to_rename->attributes.s.attrs.ctime = time(NULL);

    // Write attributes of renamed file
    if (export_lv2_write_attributes(e->trk_tb_p,lv2_to_rename) != 0)
        goto out;

    memcpy(attrs,&lv2_to_rename->attributes,sizeof(mattr_t));
    status = 0;

out:
    if( old_parent_fdp !=1) close(old_parent_fdp);
    if( new_parent_fdp !=1) close(new_parent_fdp);
    if( to_replace_fdp !=1) close(to_replace_fdp);
    if(lv2_old_parent!=0 ) export_dir_flush_root_idx_bitmap(e,pfid,lv2_old_parent->dirent_root_idx_p);
    if(lv2_new_parent!=0 ) export_dir_flush_root_idx_bitmap(e,npfid,lv2_new_parent->dirent_root_idx_p);
    if(lv2_to_rename!=0 )  export_dir_flush_root_idx_bitmap(e,fid_to_rename,lv2_to_rename->dirent_root_idx_p);
    if(lv2_to_replace!=0 )  export_dir_flush_root_idx_bitmap(e,fid_to_replace,lv2_to_replace->dirent_root_idx_p);


    STOP_PROFILING(export_rename);

    return status;
}

/*
**______________________________________________________________________________
*/
int64_t export_read(export_t * e, fid_t fid, uint64_t offset, uint32_t len,
        uint64_t * first_blk, uint32_t * nb_blks) {
    lv2_entry_t *lv2 = NULL;
    int64_t length = -1;
    uint64_t i_first_blk = 0;
    uint64_t i_last_blk = 0;
    uint32_t i_nb_blks = 0;

    START_PROFILING(export_read);

    // Get the lv2 entry
    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid))) {
        goto error;
    }

    // EOF ?
    if (offset > lv2->attributes.s.attrs.size) {
        errno = 0;
        goto error;
    }

    // Length to read
    length = (offset + len < lv2->attributes.s.attrs.size ? len : lv2->attributes.s.attrs.size - offset);
    // Nb. of the first block to read
    i_first_blk = offset / ROZOFS_BSIZE_BYTES(e->bsize);
    // Nb. of the last block to read
    i_last_blk = (offset + length) / ROZOFS_BSIZE_BYTES(e->bsize) + ((offset + length) % ROZOFS_BSIZE_BYTES(e->bsize) == 0 ? -1 : 0);
    // Nb. of blocks to read
    i_nb_blks = (i_last_blk - i_first_blk) + 1;

    *first_blk = i_first_blk;
    *nb_blks = i_nb_blks;

    // Managed access time
    if ((lv2->attributes.s.attrs.atime = time(NULL)) == -1)
        goto error;

    // Write attributes of file
    if (export_lv2_write_attributes(e->trk_tb_p,lv2) != 0)
        goto error;

    // Return the length that can be read
    goto out;

error:
    length = -1;
out:
    STOP_PROFILING(export_read);

    return length;
}
/*
**______________________________________________________________________________
*/
int export_read_block(export_t *e, fid_t fid, bid_t bid, uint32_t n, dist_t * d) {
    int status = 0;
    lv2_entry_t *lv2 = NULL;

    START_PROFILING(export_read_block);

    // Get the lv2 entry
    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid)))
        goto out;

    status = mreg_read_dist(&lv2->container.mreg, bid, n, d);
out:
    STOP_PROFILING(export_read_block);

    return status;
}

/* not used anymore
int64_t export_write(export_t *e, fid_t fid, uint64_t off, uint32_t len) {
    lv2_entry_t *lv2;

    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid))) {
        return -1;
    }

    if (off + len > lv2->attributes.s.attrs.size) {
        // Don't skip intermediate computation to keep ceil rounded
        uint64_t nbold = (lv2->attributes.s.attrs.size + ROZOFS_BSIZE - 1) / ROZOFS_BSIZE;
        uint64_t nbnew = (off + len + ROZOFS_BSIZE - 1) / ROZOFS_BSIZE;

        if (export_update_blocks(e, nbnew - nbold) != 0)
            return -1;

        lv2->attributes.s.attrs.size = off + len;
    }

    lv2->attributes.s.attrs.mtime = lv2->attributes.s.attrs.ctime = time(NULL);

    if (export_lv2_write_attributes(e->trk_tb_p,lv2) != 0)
        return -1;

    return len;
}*/
/*
**______________________________________________________________________________
*/
/**  update the file size, mtime and ctime
 *
 * dist is the same for all blocks
 *
 * @param e: the export managing the file
 * @param fid: id of the file to read
 * @param bid: first block address (from the start of the file)
 * @param n: number of blocks
 * @param d: distribution to set
 * @param off: offset to write from
 * @param len: length written
 * @param site_number: siet number for geo-replication
 * @param geo_wr_start: write start offset
 * @param geo_wr_end: write end offset
 * @param[out] attrs: updated attributes of the file
 *
 * @return: the written length on success or -1 otherwise (errno is set)
 */
int64_t export_write_block(export_t *e, fid_t fid, uint64_t bid, uint32_t n,
                           dist_t d, uint64_t off, uint32_t len,
			   uint32_t site_number,uint64_t geo_wr_start,uint64_t geo_wr_end,
	                   mattr_t *attrs) {
    int64_t length = -1;
    lv2_entry_t *lv2 = NULL;

    START_PROFILING(export_write_block);

    // Get the lv2 entry
    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid)))
        goto out;


    // Update size of file
    if (off + len > lv2->attributes.s.attrs.size) {
        // Don't skip intermediate computation to keep ceil rounded
        uint64_t nbold = (lv2->attributes.s.attrs.size + ROZOFS_BSIZE_BYTES(e->bsize) - 1) / ROZOFS_BSIZE_BYTES(e->bsize);
        uint64_t nbnew = (off + len + ROZOFS_BSIZE_BYTES(e->bsize) - 1) / ROZOFS_BSIZE_BYTES(e->bsize);

        if (export_update_blocks(e, nbnew - nbold) != 0)
            goto out;

        lv2->attributes.s.attrs.size = off + len;
    }

    // Update mtime and ctime
    lv2->attributes.s.attrs.mtime = lv2->attributes.s.attrs.ctime = time(NULL);
    if (export_lv2_write_attributes(e->trk_tb_p,lv2) != 0)
        goto out;
    /*
    ** return the parent attributes
    */
    memcpy(attrs, &lv2->attributes.s.attrs, sizeof (mattr_t));
    length = len;
    if (e->volume->georep) 
    {
      /*
      ** update the geo replication
      */
      geo_rep_insert_fid(e->geo_replication_tb[site_number],
                	 fid,geo_wr_start,geo_wr_end,
			 e->layout,
			 lv2->attributes.s.attrs.cid,
			 lv2->attributes.s.attrs.sids);
    }
out:
    STOP_PROFILING(export_write_block);

    return length;
}
/*
**______________________________________________________________________________
*/
/** read a directory
 *
 * @param e: the export managing the file
 * @param fid: the id of the directory
 * @param children: pointer to pointer where the first children we will stored
 * @param cookie: index mdirentries where we must begin to list the mdirentries
 * @param eof: pointer that indicates if we list all the entries or not
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int export_readdir(export_t * e, fid_t fid, uint64_t * cookie,
        child_t ** children, uint8_t * eof) {
    int status = -1;
    lv2_entry_t *parent = NULL;
    int fdp = -1;

    START_PROFILING(export_readdir);

    // Get the lv2 inode
    if (!(parent = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid))) {
        severe("export_readdir failed: %s", strerror(errno));
        goto out;
    }
    /*
    ** load the root_idx bitmap of the old parent
    */
    export_dir_load_root_idx_bitmap(e,fid,parent);
    
    // Verify that the target is a directory
    if (!S_ISDIR(parent->attributes.s.attrs.mode)) {
        severe("export_readdir failed: %s", strerror(errno));
        errno = ENOTDIR;
        goto out;
    }

    // List directory
    fdp = export_open_parent_directory(e,fid);
    if (fdp == -1) goto out;
    if (list_mdirentries(parent->dirent_root_idx_p,fdp, fid, children, cookie, eof) != 0) {
        goto out;
    }

    // Access time of the directory is not changed any more on readdir

    
    // Update atime of parent
    //parent->attributes.atime = time(NULL);
    //if (export_lv2_write_attributes(e->trk_tb_p,parent) != 0)
    //    goto out;

    status = 0;
out:
    if (parent != NULL) export_dir_flush_root_idx_bitmap(e,fid,parent->dirent_root_idx_p);

    if (fdp != -1) close(fdp);
    STOP_PROFILING(export_readdir);

    return status;
}
/*
**______________________________________________________________________________
*/
/** Display RozoFS special xattribute 
 *
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
#define ROZOFS_XATTR "rozofs"
#define ROZOFS_USER_XATTR "user.rozofs"
#define ROZOFS_ROOT_XATTR "trusted.rozofs"

#define DISPLAY_ATTR_TITLE(name) p += sprintf(p,"%-7s : ",name);
#define DISPLAY_ATTR_INT(name,val) p += sprintf(p,"%-7s : %d\n",name,val);
#define DISPLAY_ATTR_2INT(name,val1,val2) p += sprintf(p,"%-7s : %d/%d\n",name,val1,val2);
#define DISPLAY_ATTR_TXT(name,val) p += sprintf(p,"%-7s : %s\n",name,val);
static inline int get_rozofs_xattr(export_t *e, lv2_entry_t *lv2, char * value, int size) {
  char    * p=value;
  uint8_t * pFid;
  int       idx;
  int       left;
  uint8_t   rozofs_safe = rozofs_get_rozofs_safe(e->layout);
  
  pFid = (uint8_t *) lv2->attributes.s.attrs.fid;  
  DISPLAY_ATTR_INT("EID", e->eid);
  DISPLAY_ATTR_INT("LAYOUT", e->layout);  
  DISPLAY_ATTR_INT("BSIZE", e->bsize);  
  
  DISPLAY_ATTR_TITLE( "FID"); 
  p += sprintf(p,"%2.2x%2.2x%2.2x%2.2x-%2.2x%2.2x-%2.2x%2.2x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x\n", 
               pFid[0],pFid[1],pFid[2],pFid[3],pFid[4],pFid[5],pFid[6],pFid[7],
	       pFid[8],pFid[9],pFid[10],pFid[11],pFid[12],pFid[13],pFid[14],pFid[15]);

  DISPLAY_ATTR_2INT("UID/GID",lv2->attributes.s.attrs.uid,lv2->attributes.s.attrs.gid);


  if (S_ISDIR(lv2->attributes.s.attrs.mode)) {
    DISPLAY_ATTR_TXT("MODE", "DIRECTORY");
    DISPLAY_ATTR_INT("CHILDREN",lv2->attributes.s.attrs.children);
    return (p-value);  
  }

  if (S_ISLNK(lv2->attributes.s.attrs.mode)) {
    DISPLAY_ATTR_TXT("MODE", "SYMBOLIC LINK");
  }  
  else {
    DISPLAY_ATTR_TXT("MODE", "REGULAR FILE");
  }
  
  /*
  ** File only
  */
  DISPLAY_ATTR_INT("CLUSTER",lv2->attributes.s.attrs.cid);
  DISPLAY_ATTR_TITLE("STORAGE");
  p += sprintf(p, "%3.3d", lv2->attributes.s.attrs.sids[0]);  
  for (idx = 1; idx < rozofs_safe; idx++) {
    p += sprintf(p,"-%3.3d", lv2->attributes.s.attrs.sids[idx]);
  } 
  p += sprintf(p,"\n");

  DISPLAY_ATTR_INT("NLINK",lv2->attributes.s.attrs.nlink);
  DISPLAY_ATTR_INT("SIZE",(int)lv2->attributes.s.attrs.size);


  DISPLAY_ATTR_INT("LOCK",lv2->nb_locks);  
  if (lv2->nb_locks != 0) {
    rozofs_file_lock_t *lock_elt;
    list_t             * pl;
    char               * sizeType;


    /* Check for left space */
    left = size;
    left -= ((int)(p-value));
    if (left < 110) {
      if (left > 4) p += sprintf(p,"...");
      return (p-value);
    }
    

    /* List the locks */
    list_for_each_forward(pl, &lv2->file_lock) {

      lock_elt = list_entry(pl, rozofs_file_lock_t, next_fid_lock);	
      switch(lock_elt->lock.user_range.size) {
        case EP_LOCK_TOTAL:      sizeType = "TOTAL"; break;
	case EP_LOCK_FROM_START: sizeType = "START"; break;
	case EP_LOCK_TO_END:     sizeType = "END"; break;
	case EP_LOCK_PARTIAL:    sizeType = "PARTIAL"; break;
	default:                 sizeType = "??";
      }  
      p += sprintf(p,"   %-5s %-7s client %16.16llx owner %16.16llx [%"PRIu64":%"PRIu64"[ [%"PRIu64":%"PRIu64"[\n",
	       (lock_elt->lock.mode==EP_LOCK_WRITE)?"WRITE":"READ",sizeType, 
	       (long long unsigned int)lock_elt->lock.client_ref, 
	       (long long unsigned int)lock_elt->lock.owner_ref,
	       (uint64_t) lock_elt->lock.user_range.offset_start,
	       (uint64_t) lock_elt->lock.user_range.offset_stop,
	       (uint64_t) lock_elt->lock.effective_range.offset_start,
	       (uint64_t) lock_elt->lock.effective_range.offset_stop);

    }       
  } 

  return (p-value);  
} 
static inline int set_rozofs_xattr(export_t *e, lv2_entry_t *lv2, char * value,int length) {
  char       * p=value;
  int          idx,jdx;
  int          new_cid;
  int          new_sids[ROZOFS_SAFE_MAX]; 
  uint8_t      rozofs_safe;

  if (S_ISDIR(lv2->attributes.s.attrs.mode)) {
    errno = EISDIR;
    return -1;
  }
     
  if (S_ISLNK(lv2->attributes.s.attrs.mode)) {
    errno = EMLINK;
    return -1;
  }

  /*
  ** File must not yet be written 
  */
  if (lv2->attributes.s.attrs.size != 0) {
    errno = EFBIG;
    return -1;
  } 
  
  /*
  ** Scan value
  */
  rozofs_safe = rozofs_get_rozofs_safe(e->layout);
  memset (new_sids,0,sizeof(new_sids));
  new_cid = 0;

  errno = 0;
  new_cid = strtol(p,&p,10);
  if (errno != 0) return -1; 
  
  for (idx=0; idx < rozofs_safe; idx++) {
  
    if ((p-value)>=length) {
      errno = EINVAL;
      break;
    }

    new_sids[idx] = strtol(p,&p,10);
    if (errno != 0) return -1;
    if (new_sids[idx]<0) new_sids[idx] *= -1;
  }
  
  /* Only cluster id is given */
  if (idx == 0) {
    for (idx=0; idx < rozofs_safe; idx++) {
      new_sids[idx] = lv2->attributes.s.attrs.sids[idx];
    }
  }
   
  /* Not enough sid in the list */
  else if (idx != rozofs_safe) {
    return -1;
  }
  
  
  /*
  ** Check the same sid is not set 2 times
  */
  for (idx=0; idx < rozofs_safe; idx++) {
    for (jdx=idx+1; jdx < rozofs_safe; jdx++) {
      if (new_sids[idx] == new_sids[jdx]) {
        errno = EINVAL;
	return -1;
      }
    }
  }  

  /*
  ** Check cluster and sid exist
  */
  if (volume_distribution_check(e->volume, rozofs_safe, new_cid, new_sids) != 0) return -1;
  
  /*
  ** OK for the new distribution
  */
  lv2->attributes.s.attrs.cid = new_cid;
  for (idx=0; idx < rozofs_safe; idx++) {
    lv2->attributes.s.attrs.sids[idx] = new_sids[idx];
  }
  
  /*
  ** Save new distribution on disk
  */
  return export_lv2_write_attributes(e->trk_tb_p,lv2);  
} 
/*
**______________________________________________________________________________
*/
/** retrieve an extended attribute value.
 *
 * @param e: the export managing the file or directory.
 * @param fid: the id of the file or directory.
 * @param name: the extended attribute name.
 * @param value: the value of this extended attribute.
 * @param size: the size of a buffer to hold the value associated
 *  with this extended attribute.
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
ssize_t export_getxattr(export_t *e, fid_t fid, const char *name, void *value, size_t size) {
    ssize_t status = -1;
    lv2_entry_t *lv2 = 0;
    void * buffer;

    START_PROFILING(export_getxattr);
    /*
    ** check if the request is just for the xattr len: if it the case set the buffer to NULL
    */
    if (size == 0) buffer = 0;
    else buffer = value;

    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid))) {
        severe("export_getattr failed: %s", strerror(errno));
        goto out;
    }

    if ((strcmp(name,ROZOFS_XATTR)==0)||(strcmp(name,ROZOFS_USER_XATTR)==0)||(strcmp(name,ROZOFS_ROOT_XATTR)==0)) {
      status = get_rozofs_xattr(e,lv2,value,size);
      goto out;
    }  
    {
      struct dentry entry;
      entry.d_inode = lv2;
      entry.trk_tb_p = e->trk_tb_p;
    
      if ((status = rozofs_getxattr(&entry, name, buffer, size)) != 0) {
          goto out;
      }
    }
out:
    STOP_PROFILING(export_getxattr);

    return status;
}
/*
**______________________________________________________________________________
*/
/** Set a lock on a file
 *
 * @param e: the export managing the file or directory.
 * @param fid: the id of the file or directory.
 * @param lock: the lock to set/remove
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
int export_set_file_lock(export_t *e, fid_t fid, ep_lock_t * lock_requested, ep_lock_t * blocking_lock) {
    ssize_t status = -1;
    lv2_entry_t *lv2 = 0;
    list_t      *p;
    rozofs_file_lock_t * lock_elt;
    rozofs_file_lock_t * new_lock;
    int                  overlap=0;

    START_PROFILING(export_set_file_lock);

    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid))) {
        severe("export_set_lock failed: %s", strerror(errno));
        goto out;
    }

    /*
    ** Freeing a lock 
    */
    if (lock_requested->mode == EP_LOCK_FREE) {
    
      /* Always succcess */
      status = 0;

      /* Already free */
      if (lv2->nb_locks == 0) {
	goto out;
      }
      if (list_empty(&lv2->file_lock)) {
	lv2->nb_locks = 0;
	goto out;
      }  
      
reloop:       
      /* Search the given lock */
      list_for_each_forward(p, &lv2->file_lock) {
      
        lock_elt = list_entry(p, rozofs_file_lock_t, next_fid_lock);	
	
	if (must_file_lock_be_removed(e->bsize,lock_requested, &lock_elt->lock, &new_lock)) {
	  lv2_cache_free_file_lock(lock_elt);
	  lv2->nb_locks--;
	  if (list_empty(&lv2->file_lock)) {
	    lv2->nb_locks = 0;
	    goto out;
	  }
	  goto reloop;
	}

	if (new_lock) {
	  list_push_front(&lv2->file_lock,&new_lock->next_fid_lock);
	  lv2->nb_locks++;
	}
      }
      goto out; 
    }

    /*
    ** Setting a new lock. Check its compatibility against every already set lock
    */
    list_for_each_forward(p, &lv2->file_lock) {
    
      lock_elt = list_entry(p, rozofs_file_lock_t, next_fid_lock);
      
      /*
      ** Check compatibility between 2 different applications
      */
      if ((lock_elt->lock.client_ref != lock_requested->client_ref) 
      ||  (lock_elt->lock.owner_ref != lock_requested->owner_ref)) { 
      
	if (!are_file_locks_compatible(&lock_elt->lock,lock_requested)) {
	  memcpy(blocking_lock,&lock_elt->lock,sizeof(ep_lock_t));     
          errno = EWOULDBLOCK;
	  goto out;      
	} 
    	continue;    
      }
      
      /*
      ** Check compatibility of 2 locks of a same application
      */

      /*
      ** Two read or two write locks. Check whether they overlap
      */
      if (lock_elt->lock.mode == lock_requested->mode) {
        if (are_file_locks_overlapping(lock_requested,&lock_elt->lock)) {
	  overlap++;
	}  
        continue;
      }
      
      /*
      ** One read and one write
      */
      if (!are_file_locks_compatible(&lock_elt->lock,lock_requested)) {
	memcpy(blocking_lock,&lock_elt->lock,sizeof(ep_lock_t));     
        errno = EWOULDBLOCK;
	goto out;      
      }     
      continue; 			  
    }

    /*
    ** This lock overlaps with a least one existing lock of the same application.
    ** Let's concatenate all those locks
    */  
concatenate:  
    if (overlap != 0) {
      list_for_each_forward(p, &lv2->file_lock) {

	lock_elt = list_entry(p, rozofs_file_lock_t, next_fid_lock);

	if ((lock_elt->lock.client_ref != lock_requested->client_ref) 
	||  (lock_elt->lock.owner_ref != lock_requested->owner_ref)) continue;

	if (lock_elt->lock.mode != lock_requested->mode) continue;

	if (try_file_locks_concatenate(e->bsize,lock_requested,&lock_elt->lock)) {
          overlap--;
	  lv2_cache_free_file_lock(lock_elt);
	  lv2->nb_locks--;
	  if (list_empty(&lv2->file_lock)) {
	    lv2->nb_locks = 0;
	  }
	  goto concatenate;	  
	}
      } 
    }   
        
    /*
    ** Since we have reached this point all the locks are compatibles with the new one.
    ** and it does not overlap any more with an other lock. Let's insert this new lock
    */
    lock_elt = lv2_cache_allocate_file_lock(lock_requested);
    list_push_front(&lv2->file_lock,&lock_elt->next_fid_lock);
    lv2->nb_locks++;
    status = 0; 
    
out:
#if 0
    {
      char BuF[4096];
      char * pChar = BuF;
      debug_file_lock_list(pChar);
      info("%s",BuF);
    }
#endif       
    STOP_PROFILING(export_set_file_lock);
    return status;
}
/*
**______________________________________________________________________________
*/
/** Get a lock on a file
 *
 * @param e: the export managing the file or directory.
 * @param fid: the id of the file or directory.
 * @param lock: the lock to set/remove
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
int export_get_file_lock(export_t *e, fid_t fid, ep_lock_t * lock_requested, ep_lock_t * blocking_lock) {
    ssize_t status = -1;
    lv2_entry_t *lv2 = 0;
    rozofs_file_lock_t *lock_elt;
    list_t * p;

    START_PROFILING(export_get_file_lock);

    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid))) {
        severe("export_get_lock failed: %s", strerror(errno));
        goto out;
    }

    /*
    ** Freeing a lock 
    */
    if (lock_requested->mode == EP_LOCK_FREE) {    
      /* Always succcess */
      status = 0;
      goto out; 
    }

    /*
    ** Setting a new lock. Check its compatibility against every already set lock
    */
    list_for_each_forward(p, &lv2->file_lock) {
    
      lock_elt = list_entry(p, rozofs_file_lock_t, next_fid_lock);

      if (!are_file_locks_compatible(&lock_elt->lock,lock_requested)) {
	memcpy(blocking_lock,&lock_elt->lock,sizeof(ep_lock_t));     
        errno = EWOULDBLOCK;
	goto out;      
      }     
    }
    status = 0;
    
out:
    STOP_PROFILING(export_get_file_lock);
    return status;
}
/*
**______________________________________________________________________________
*/
/** reset a lock from a client
 *
 * @param e: the export managing the file or directory.
 * @param lock: the identifier of the client whose locks are to remove
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
int export_clear_client_file_lock(export_t *e, ep_lock_t * lock_requested) {

    START_PROFILING(export_clearclient_flock);
    file_lock_remove_client(lock_requested->client_ref);
    STOP_PROFILING(export_clearclient_flock);
    return 0;
}
/*
**______________________________________________________________________________
*/
/** reset all the locks from an owner
 *
 * @param e: the export managing the file or directory.
 * @param lock: the identifier of the client whose locks are to remove
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
int export_clear_owner_file_lock(export_t *e, fid_t fid, ep_lock_t * lock_requested) {
    int status = -1;
    lv2_entry_t *lv2 = 0;
    list_t * p;
    rozofs_file_lock_t *lock_elt;
    
    START_PROFILING(export_clearowner_flock);

    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid))) {
        severe("export_lookup_fid failed: %s", strerror(errno));
        goto out;
    }
    
    status = 0;

reloop:    
    /* Search the given lock */
    list_for_each_forward(p, &lv2->file_lock) {
      lock_elt = list_entry(p, rozofs_file_lock_t, next_fid_lock);
      if ((lock_elt->lock.client_ref == lock_requested->client_ref) &&
          (lock_elt->lock.owner_ref == lock_requested->owner_ref)) {
	  /* Found a lock to free */
	  lv2_cache_free_file_lock(lock_elt);
	  lv2->nb_locks--;
	  if (list_empty(&lv2->file_lock)) {
	    lv2->nb_locks = 0;
	    break;
	  }
	  goto reloop;
      }       
    }    

out:
    STOP_PROFILING(export_clearowner_flock);
    return status;
}
/*
**______________________________________________________________________________
*/
/** Get a poll event from a client
 *
 * @param e: the export managing the file or directory.
 * @param lock: the lock to set/remove
 * 
 * @return: On success, the size of the extended attribute value.
 * On failure, -1 is returned and errno is set appropriately.
 */
int export_poll_file_lock(export_t *e, ep_lock_t * lock_requested) {

    START_PROFILING(export_poll_file_lock);
    file_lock_poll_client(lock_requested->client_ref);
    STOP_PROFILING(export_poll_file_lock);
    return 0;
}
/*
**______________________________________________________________________________
*/
/** set an extended attribute value for a file or directory.
 *
 * @param e: the export managing the file or directory.
 * @param fid: the id of the file or directory.
 * @param name: the extended attribute name.
 * @param value: the value of this extended attribute.
 * @param size: the size of a buffer to hold the value associated
 *  with this extended attribute.
 * @param flags: parameter can be used to refine the semantics of the operation.
 * 
 * @return: On success, zero is returned.  On failure, -1 is returned.
 */
int export_setxattr(export_t *e, fid_t fid, char *name, const void *value, size_t size, int flags) {
    int status = -1;
    lv2_entry_t *lv2 = 0;

    START_PROFILING(export_setxattr);

    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid))) {
        severe("export_getattr failed: %s", strerror(errno));
        goto out;
    }


    if ((strcmp(name,ROZOFS_XATTR)==0)||(strcmp(name,ROZOFS_USER_XATTR)==0)||(strcmp(name,ROZOFS_ROOT_XATTR)==0)) {
      status = set_rozofs_xattr(e,lv2,(char *)value,size);
      goto out;
    }  
    {
      struct dentry entry;
      entry.d_inode = lv2;
      entry.trk_tb_p = e->trk_tb_p;
    
      if ((status = rozofs_setxattr(&entry, name, value, size, flags)) != 0) {
          goto out;
      }
    }

    status = 0;
out:
    STOP_PROFILING(export_setxattr);

    return status;
}
/*
**______________________________________________________________________________
*/
/** remove an extended attribute from a file or directory.
 *
 * @param e: the export managing the file or directory.
 * @param fid: the id of the file or directory.
 * @param name: the extended attribute name.
 * 
 * @return: On success, zero is returned.  On failure, -1 is returned.
 */
int export_removexattr(export_t *e, fid_t fid, char *name) {
    int status = -1;
    lv2_entry_t *lv2 = 0;

    START_PROFILING(export_removexattr);

    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid))) {
        severe("export_getattr failed: %s", strerror(errno));
        goto out;
    }

    {
      struct dentry entry;
      entry.d_inode = lv2;
      entry.trk_tb_p = e->trk_tb_p;
    
      if ((status = rozofs_removexattr(&entry, name)) != 0) {
          goto out;
      }
    }

    status = 0;
out:
    STOP_PROFILING(export_removexattr);

    return status;
}
/*
**______________________________________________________________________________
*/
/** list extended attribute names from the lv2 regular file.
 *
 * @param e: the export managing the file or directory.
 * @param fid: the id of the file or directory.
 * @param list: list of extended attribute names associated with this file/dir.
 * @param size: the size of a buffer to hold the list of extended attributes.
 * 
 * @return: On success, the size of the extended attribute name list.
 * On failure, -1 is returned and errno is set appropriately.
 */
ssize_t export_listxattr(export_t *e, fid_t fid, void *list, size_t size) {
    ssize_t status = -1;
    lv2_entry_t *lv2 = 0;

    START_PROFILING(export_listxattr);

    if (!(lv2 = export_lookup_fid(e->trk_tb_p,e->lv2_cache, fid))) {
        severe("export_getattr failed: %s", strerror(errno));
        goto out;
    }
    {
      struct dentry entry;
      entry.d_inode = lv2;
      entry.trk_tb_p = e->trk_tb_p;
    
      if ((status = rozofs_listxattr(&entry, list,size)) != 0) {
          goto out;
      }
    }


out:
    STOP_PROFILING(export_listxattr);
    return status;
}

/*
int export_open(export_t * e, fid_t fid) {
    int flag;

    flag = O_RDWR;

    if (!(mfe = export_get_mfentry_by_id(e, fid))) {
        severe("export_open failed: export_get_mfentry_by_id failed");
        goto out;
    }

    if (mfe->fd == -1) {
        if ((mfe->fd = open(mfe->path, flag)) < 0) {
            severe("export_open failed for file %s: %s", mfe->path,
                    strerror(errno));
            goto out;
        }
    }

    mfe->cnt++;

    status = 0;
out:

    return status;
}

int export_close(export_t * e, fid_t fid) {
    int status = -1;
    mfentry_t *mfe = 0;
    DEBUG_FUNCTION;

    if (!(mfe = export_get_mfentry_by_id(e, fid))) {
        severe("export_close failed: export_get_mfentry_by_id failed");
        goto out;
    }

    if (mfe->cnt == 1) {
        if (close(mfe->fd) != 0) {
            goto out;
        }
        mfe->fd = -1;
    }

    mfe->cnt--;

    status = 0;
out:
    return status;
}
 */
