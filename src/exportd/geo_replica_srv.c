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
#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>
#include <pthread.h>
#include <rozofs/rpc/export_profiler.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/rpc/geo_replica_proto.h>
#include <rozofs/core/uma_dbg_api.h>
#include <rozofs/core/ruc_common.h>
#include "geo_replica_proto_nb.h"
#include "geo_replica_srv.h"
#include "geo_replica_ctx.h"
#include "geo_replication.h"
#include "geo_replica_north_intf.h"

#if 0
void * decoded_rpc_buffer_pool = NULL;
#endif
//geo_rep_srv_ctx_t *geo_rep_srv_ctx_p = NULL;
uint8_t *geo_rep_bufread_p = NULL;
uint32_t geo_rep_guard_timer_ms = GEO_REP_DEFAULT_GUARD_TMR; 

geo_srv_sync_ctx_tab_t *geo_srv_sync_ctx_tab_p[EXPGW_EID_MAX_IDX+1] = { 0 };
geo_srv_sync_err_t geo_srv_sync_err_tab_p[(EXPGW_EID_MAX_IDX+1)*EXPORT_GEO_MAX_CTX];

geo_srv_sync_ctx_t *geo_srv_get_eid_site_context_ptr(int eid,uint16_t site_id);
int geo_replica_log_enable = 0;
int geo_rep_recycle_frequency;
geo_ctx_tmr_var_t geo_rep_ctx_tmr={FALSE}; 

/*
**_________________________________________________________________________
*      PUBLIC FUNCTIONS
**_________________________________________________________________________
*/
int geo_replicat_recycle_geo_sync_file(geo_proc_ctx_t *p,geo_rep_srv_ctx_t *geo_rep_srv_ctx_p);

/*
*________________________________________________
* Reset statistics of an export id
*
* @param eid The export identifier
*/    
void geo_err_reset_one(int eid) {

  geo_srv_sync_err_t *bid_ptr;
  
  if (eid>EXPGW_EXPORTD_MAX_IDX) return;
  bid_ptr = &geo_srv_sync_err_tab_p[(eid<<EXPORT_GEO_MAX_BIT)];
  memset(bid_ptr,0,sizeof(geo_srv_sync_err_t)*EXPORT_GEO_MAX_CTX);

} 

/*  
*________________________________________________
* Reset statistics of all export ids

*/    
void geo_err_reset_all() {
  int eid;
  for (eid=0; eid <= EXPGW_EXPORTD_MAX_IDX; eid++) {
      geo_err_reset_one(eid);   
  }
}   

#define SHOW_GEO_ERR_PROBE(probe) if ((geo_srv_sync_err_tab_p[(eid<<EXPORT_GEO_MAX_BIT)].err_stats[probe]!= 0) ||\
                                          (geo_srv_sync_err_tab_p[(eid<<EXPORT_GEO_MAX_BIT)| 1].err_stats[probe]!=0)) \
                    pChar += sprintf(pChar," %-24s | %15"PRIu64" | %15"PRIu64" |\n",\
                    #probe,\
                    geo_srv_sync_err_tab_p[(eid<<EXPORT_GEO_MAX_BIT)].err_stats[probe],\
                    geo_srv_sync_err_tab_p[(eid<<EXPORT_GEO_MAX_BIT)| 1].err_stats[probe]);


char * show_geo_err_one(char * pChar, uint32_t eid) {


    if (eid>EXPGW_EID_MAX_IDX) return pChar;
    
    if ((geo_srv_sync_err_tab_p[(eid<<EXPORT_GEO_MAX_BIT)].err_stats[GEO_ERR_ALL_ERR]== 0) &&
        (geo_srv_sync_err_tab_p[(eid<<EXPORT_GEO_MAX_BIT)| 1].err_stats[GEO_ERR_ALL_ERR]==0))
       return pChar;
       
    pChar +=  sprintf(pChar, "_______________________ EID = %d _______________________ \n",eid);
    pChar += sprintf(pChar, "   procedure              |     site 0      |     site 1      |\n");
    pChar += sprintf(pChar, "--------------------------+-----------------+-----------------+\n");
    SHOW_GEO_ERR_PROBE(GEO_ERR_SYNC_FILE_OPEN_ERR);
    SHOW_GEO_ERR_PROBE(GEO_ERR_SYNC_FILE_ENOENT);
    SHOW_GEO_ERR_PROBE(GEO_ERR_SYNC_FILE_READ_ERR);
    SHOW_GEO_ERR_PROBE(GEO_ERR_SYNC_FILE_READ_MISMATCH);
    SHOW_GEO_ERR_PROBE(GEO_ERR_SYNC_FILE_STAT_ERR);
    SHOW_GEO_ERR_PROBE(GEO_ERR_SYNC_FILE_UNLINK_ERR);
    SHOW_GEO_ERR_PROBE(GEO_ERR_SYNC_CTX_UNAVAILABLE);
    SHOW_GEO_ERR_PROBE(GEO_ERR_SYNC_CTX_ERANGE);
    SHOW_GEO_ERR_PROBE(GEO_ERR_SYNC_CTX_MISMATCH);
    SHOW_GEO_ERR_PROBE(GEO_ERR_ROOT_CTX_UNAVAILABLE);
    SHOW_GEO_ERR_PROBE(GEO_ERR_IDX_FILE_READ_ERR);
    SHOW_GEO_ERR_PROBE(GEO_ERR_IDX_FILE_WRITE_ERR);
    SHOW_GEO_ERR_PROBE(GEO_ERR_IDX_FILE_RENAME_ERR);

    return pChar;
}


static char * show_geo_err_help(char * pChar) {
  pChar += sprintf(pChar,"usage:\n");
  pChar += sprintf(pChar,"geo_err reset [ <eid> ] : reset statistics\n");
  pChar += sprintf(pChar,"geo_err log             : enable error logging in syslog\n");
  pChar += sprintf(pChar,"geo_err no_log          : disable error logging in syslog\n");
  pChar += sprintf(pChar,"geo_err [ <eid> ]       : display statistics\n");  
  return pChar; 
}
void show_geo_err(char * argv[], uint32_t tcpRef, void *bufRef) {
    char *pChar = uma_dbg_get_buffer();
    uint32_t eid;
    int ret;

    pChar +=sprintf(pChar,"syslog is %s for geo-replication errors\n",(geo_replica_log_enable==1)?"enabled":"disabled");
    if (argv[1] == NULL) {
      for (eid=0; eid <= EXPGW_EID_MAX_IDX; eid++) 
        pChar = show_geo_err_one(pChar,eid);
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   	  
      return;
    }

    if (strcmp(argv[1],"log")==0) {
      geo_replica_log_enable = 1;
      uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   	  
      return;
    }

    if (strcmp(argv[1],"no_log")==0) {
      geo_replica_log_enable = 0;
      uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   	  
      return;
    }

    if (strcmp(argv[1],"reset")==0) {

      if (argv[2] == NULL) {
	geo_err_reset_all();
	uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   
	return;	 
      }
      	     
      ret = sscanf(argv[2], "%d", &eid);
      if (ret != 1) {
        show_geo_err_help(pChar);	
	uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
	return;   
      }
      geo_err_reset_one(eid);
      uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   	  
      return;
    }

    ret = sscanf(argv[1], "%d", &eid);
    if (ret != 1) {
      show_geo_err_help(pChar);	
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;   
    }
    pChar = show_geo_err_one(pChar,eid);
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   	  
    return;
}


#define SHOW_GEO_STATS_PROBE(site,file_pending,file_synced,rate) \
                    pChar += sprintf(pChar," %-4d | %15"PRIu64" | %15"PRIu64" | %15"PRIu64" | %9"PRIu32" | %15"PRIu64" |\n",\
                    site,\
                    file_pending,\
                    file_synced,\
                    (file_synced > file_pending)?0:file_pending- file_synced, \
		    rate,\
		    (rate==0)?0:(file_pending- file_synced)/rate);


char * show_geo_rep_stats_one(char * pChar, uint32_t eid) {
    int i;
    geo_srv_sync_ctx_t *root_p;
    uint64_t file_pending;
    uint64_t file_synced;
    int header_print = 0;

    if (eid>EXPGW_EID_MAX_IDX) return pChar;
    
    for (i = 0; i < EXPORT_GEO_MAX_CTX; i++) 
    {
      root_p = geo_srv_get_eid_site_context_ptr(eid,i);
      if (root_p == NULL) return pChar;
      
      geo_rep_read_sync_stats(&root_p->parent,&file_pending,&file_synced);
      if (header_print == 0)
      {
	// Compute uptime for storaged process
	pChar +=  sprintf(pChar, "_______________________ EID = %d _______________________ \n",eid);
	pChar += sprintf(pChar, " site |     pending     |     synced      |   remaining     |  rate     |    delay (s)    |\n");
	pChar += sprintf(pChar, "------+-----------------+-----------------+-----------------+-----------+-----------------+\n");
	header_print = 1;
      } 
      SHOW_GEO_STATS_PROBE(i,file_pending,file_synced,root_p->parent.synchro_rate);
    }
    return pChar;
}


static char * show_geo_rep_stats_help(char * pChar) {
  pChar += sprintf(pChar,"usage:\n");
//  pChar += sprintf(pChar,"geo_stats reset [ <eid> ] : reset statistics\n");
  pChar += sprintf(pChar,"geo_stats [ <eid> ]       : display statistics\n");  
  return pChar; 
}
void show_geo_rep_stats(char * argv[], uint32_t tcpRef, void *bufRef) {
    char *pChar = uma_dbg_get_buffer();
    uint32_t eid;
    int ret;
    
    *pChar = 0;

    if (argv[1] == NULL) {
      for (eid=0; eid <= EXPGW_EID_MAX_IDX; eid++) 
        pChar = show_geo_rep_stats_one(pChar,eid);
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   	  
      return;
    }
#if 0
    if (strcmp(argv[1],"reset")==0) {

      if (argv[2] == NULL) {
	geo_profiler_reset_all();
	uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   
	return;	 
      }
      	     
      ret = sscanf(argv[2], "%d", &eid);
      if (ret != 1) {
        show_geo_rep_stats_help(pChar);	
	uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
	return;   
      }
      geo_profiler_reset_one(eid);
      uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   	  
      return;
    }
#endif
    ret = sscanf(argv[1], "%d", &eid);
    if (ret != 1) {
      show_geo_rep_stats_help(pChar);	
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;   
    }
    pChar = show_geo_rep_stats_one(pChar,eid);
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   	  
    return;
}


int cur_file_display_count;
#define GEO_MAX_FILE_DISPLAY 100
/*
**______________________________________________________________________________
*/
char *display_one_file(char *pChar,int site,int idx,char *type,int nb_records,uint64_t date)
{
   char *date_p;
   
   if (cur_file_display_count == 0)
   {
      pChar += sprintf(pChar,"more ...\n");
      cur_file_display_count = -1;
      return pChar;
   }
   
   date_p = ctime((const time_t *)&date);
   if (date_p != NULL)
   {
     int len = strlen(date_p);
     date_p[len-1]= 0;
   }
   
   pChar += sprintf(pChar," %-4d |%10d/%s | %-6d | %-24s | %-24s |\n",site,idx,type,nb_records,(date_p==NULL)?"no date":date_p,strerror(errno));
   cur_file_display_count--;
   return pChar;
}
/*
**______________________________________________________________________________
*/
/**
* show the information related to the fid that are in the recycle liste
*/

char *show_recycle_files(char *pChar,geo_rep_srv_ctx_t *geo_rep_srv_ctx_p)
{
   int ret;
   char path[ROZOFS_PATH_MAX];
   struct stat buf;
   ssize_t read_len;
   uint64_t date;
   int nb_records;
   int idx;
   int fd;
   int site = geo_rep_srv_ctx_p->site_id;
    
   ret = geo_rep_disk_read_index_file_recycle(geo_rep_srv_ctx_p);
   if (ret < 0)
   {
     GEO_ERR_STATS(geo_rep_srv_ctx_p->eid,geo_rep_srv_ctx_p->site_id,GEO_ERR_IDX_FILE_READ_ERR);
     return pChar;
   }
   /*
   ** OK now scan the files that contain the fid to synchronize
   */
   for (idx = geo_rep_srv_ctx_p->geo_rep_main_recycle_file.first_index; idx < geo_rep_srv_ctx_p->geo_rep_main_recycle_file.last_index; idx++)
   {
     /*
     ** build the pathname for the current file
     */
     sprintf(path, "%s/%s_%d/%s_%llu", geo_rep_srv_ctx_p->geo_rep_export_root_path,GEO_DIR_RECYCLE,
                     geo_rep_srv_ctx_p->site_id,GEO_FILE,(long long unsigned int)idx);
     /*
     ** check the size of the file
     */
     if (stat((const char *)path, &buf) < 0)
     {
       if (errno != ENOENT) 
       {
         GEO_ERR_STATS(geo_rep_srv_ctx_p->eid,geo_rep_srv_ctx_p->site_id,GEO_ERR_SYNC_FILE_STAT_ERR);
	 continue;
       } 
       continue;
     }
     /*
     ** read the date of file
     */
     fd = open(path, O_RDWR | O_NOATIME, S_IRWXU);
     if (fd < 0)
     {
       if (errno != ENOENT) 
       {
	 GEO_ERR_STATS(geo_rep_srv_ctx_p->eid,geo_rep_srv_ctx_p->site_id,GEO_ERR_SYNC_FILE_OPEN_ERR);
       }
       continue;
     }
     errno = 0;
     read_len = pread(fd,&date,sizeof(date),0); 
     if (read_len < 0)
     {
       GEO_ERR_STATS(geo_rep_srv_ctx_p->eid,geo_rep_srv_ctx_p->site_id,GEO_ERR_SYNC_FILE_READ_ERR);
       close(fd);
       continue;
     }
     close(fd);
     nb_records = (buf.st_size - sizeof(date))/sizeof(geo_fid_entry_t);
     pChar = display_one_file(pChar,site,idx,"RCY",nb_records,date);
   }
   return pChar;
}

/*
**______________________________________________________________________________
*/
/**
* show the information related to the fid that are in the recycle liste
*/

char *show_regular_files(char *pChar,geo_rep_srv_ctx_t *geo_rep_srv_ctx_p)
{
   int ret;
   char path[ROZOFS_PATH_MAX];
   struct stat buf;
   ssize_t read_len;
   uint64_t date;
   int nb_records;
   int idx;
   int fd;
   int site = geo_rep_srv_ctx_p->site_id;
       
   ret = geo_rep_disk_read_index_file(geo_rep_srv_ctx_p);
   if (ret < 0)
   {
     GEO_ERR_STATS(geo_rep_srv_ctx_p->eid,geo_rep_srv_ctx_p->site_id,GEO_ERR_IDX_FILE_READ_ERR);
     return pChar;
   }
   /*
   ** OK now scan the files that contain the fid to synchronize
   */
   for (idx = geo_rep_srv_ctx_p->geo_rep_main_file.first_index; idx < geo_rep_srv_ctx_p->geo_rep_main_file.last_index; idx++)
   {
     /*
     ** build the pathname for the current file
     */
     sprintf(path, "%s/%s_%d/%s_%llu", geo_rep_srv_ctx_p->geo_rep_export_root_path,GEO_DIRECTORY,
                     geo_rep_srv_ctx_p->site_id,GEO_FILE,(long long unsigned int)idx);
     /*
     ** check the size of the file
     */
     if (stat((const char *)path, &buf) < 0)
     {
       if (errno != ENOENT) 
       {
         GEO_ERR_STATS(geo_rep_srv_ctx_p->eid,geo_rep_srv_ctx_p->site_id,GEO_ERR_SYNC_FILE_STAT_ERR);
	 continue;
       } 
       continue;
     }
     /*
     ** read the date of file
     */
     fd = open(path, O_RDWR | O_NOATIME, S_IRWXU);
     if (fd < 0)
     {
       if (errno != ENOENT) 
       {
	 GEO_ERR_STATS(geo_rep_srv_ctx_p->eid,geo_rep_srv_ctx_p->site_id,GEO_ERR_SYNC_FILE_OPEN_ERR);
       }
       continue;
     }
     errno = 0;
     read_len = pread(fd,&date,sizeof(date),0); 
     if (read_len < 0)
     {
       GEO_ERR_STATS(geo_rep_srv_ctx_p->eid,geo_rep_srv_ctx_p->site_id,GEO_ERR_SYNC_FILE_READ_ERR)
       close(fd);
       continue;
     }
     close(fd);
     nb_records = (buf.st_size - sizeof(date))/sizeof(geo_fid_entry_t);
     pChar = display_one_file(pChar,site,idx,"REG",nb_records,date);
   }
   return pChar;

}
/*
**______________________________________________________________________________
*/
char *show_geo_files_from_site(char *pChar,geo_rep_srv_ctx_t *geo_rep_srv_ctx_p)
{
  pChar = show_regular_files(pChar,geo_rep_srv_ctx_p);
  pChar = show_recycle_files(pChar,geo_rep_srv_ctx_p);
  return pChar;
}

/*
**______________________________________________________________________________
*/
char * show_geo_rep_files_one(char * pChar, uint32_t eid) {
    int i;
    geo_srv_sync_ctx_t *root_p;
    int header_print = 0;
    cur_file_display_count = GEO_MAX_FILE_DISPLAY;

    if (eid>EXPGW_EID_MAX_IDX) return pChar;
    
    for (i = 0; i < EXPORT_GEO_MAX_CTX; i++) 
    {
      root_p = geo_srv_get_eid_site_context_ptr(eid,i);
      if (root_p == NULL) return pChar;
      if (header_print == 0)
      {
	// Compute uptime for storaged process
	pChar +=  sprintf(pChar, "_______________________ EID = %d _______________________ \n",eid);
	pChar += sprintf(pChar, " site |   idx/type    |  count |      date                | read status              |\n");
	pChar += sprintf(pChar, "------+---------------|--------+--------------------------+--------------------------+\n");

	header_print = 1;
      }       
      pChar = show_geo_files_from_site(pChar,&root_p->parent);
    }
    return pChar;
}

/*
**______________________________________________________________________________
*/
static char * show_geo_rep_files_help(char * pChar) {
  pChar += sprintf(pChar,"usage:\n");
//  pChar += sprintf(pChar,"geo_files reset [ <eid> ] : reset statistics\n");
  pChar += sprintf(pChar,"geo_files [ <eid> ]       : display statistics\n");  
  return pChar; 
}
/*
**______________________________________________________________________________
*/
void show_geo_rep_files(char * argv[], uint32_t tcpRef, void *bufRef) {
    char *pChar = uma_dbg_get_buffer();
    uint32_t eid;
    int ret;
    
    *pChar = 0;

    if (argv[1] == NULL) {
      for (eid=0; eid <= EXPGW_EID_MAX_IDX; eid++) 
        pChar = show_geo_rep_files_one(pChar,eid);
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   	  
      return;
    }
#if 0
    if (strcmp(argv[1],"reset")==0) {

      if (argv[2] == NULL) {
	geo_profiler_reset_all();
	uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   
	return;	 
      }
      	     
      ret = sscanf(argv[2], "%d", &eid);
      if (ret != 1) {
        show_geo_rep_files_help(pChar);	
	uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
	return;   
      }
      geo_profiler_reset_one(eid);
      uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   	  
      return;
    }
#endif
    ret = sscanf(argv[1], "%d", &eid);
    if (ret != 1) {
      show_geo_rep_files_help(pChar);	
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;   
    }
    pChar = show_geo_rep_files_one(pChar,eid);
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   	  
    return;
}



/*
**______________________________________________________________________________
*/
/*
**  API coall upon a time-out on client context supervision

   @param entry_p : pointer to the client context

  @retval none
*/
void geo_rep_client_ctx_tmo(geo_proc_ctx_t *entry_p)
{
   geo_srv_sync_ctx_t *eid_site_p = NULL;
   int ret;
   geo_rep_srv_ctx_t *geo_rep_srv_ctx_p;   
   /*
   ** get the pointer to the context associated with the site_id and the eid
   */
   eid_site_p = geo_srv_get_eid_site_context_ptr(entry_p->eid,entry_p->site_id);
   if (eid_site_p == NULL) 
   {
     geo_proc_free_from_ptr(entry_p);
     return;
   }
   geo_rep_srv_ctx_p = &eid_site_p->parent;
   /*
   ** check the key in the eid/site_id working table
   */
   if (eid_site_p->working[entry_p->working_idx].local_ref.u32 == entry_p->local_ref.u32)
   {
     if (eid_site_p->working[entry_p->working_idx].state == GEO_WORK_BUSY)
     {
        /*
	** update the synchro file by removing the entry that have been synced on
	** the remote
	*/
	ret = geo_replicat_recycle_geo_sync_file(entry_p,geo_rep_srv_ctx_p);
	if (ret < 0) 
	{   
	   GEO_ERR_STATS(entry_p->eid,entry_p->site_id,GEO_ERR_SYNC_FILE_UNLINK_ERR);
	}
        eid_site_p->working[entry_p->working_idx].state = GEO_WORK_FREE;
        eid_site_p->nb_working_cur--;
        if (eid_site_p->nb_working_cur < 0) eid_site_p->nb_working_cur=0;
     }
   }
   GEO_ERR_STATS(entry_p->eid,entry_p->site_id,GEO_ERR_SYNC_CTX_TIMEOUT);
   geo_proc_free_from_ptr(entry_p);
}

/*
**______________________________________________________________________________
*/
/**
*   Get the context associated with an eid and a site_id
    If the context is not found a fresh one is allocated

    @param eid: export identifier
    @param site_id : source site identifier
    
    @retval <> NULL: pointer to the associated context
    @retval NULL: out of memory
*/
geo_srv_sync_ctx_t *geo_srv_get_eid_site_context_ptr(int eid,uint16_t site_id)
{
   if (EXPORT_GEO_MAX_CTX <= site_id)
   {
      //severe("site is out of range %d max is %d",site_id,EXPORT_GEO_MAX_CTX);
      errno = ERANGE;
      return NULL;
   }
   
   geo_srv_sync_ctx_tab_t *tab_p = geo_srv_sync_ctx_tab_p[eid];
   if (tab_p == NULL)
   {
      return NULL;
   }
   /*
   ** check if there is an entry for the site_id
   */
   return (tab_p->site_table_p[site_id]);
}
/*
**______________________________________________________________________________
*/
/**
*   Get the context associated with an eid and a site_id
    If the context is not found a fresh one is allocated

    @param e: pointer to the export data structure
    @param site_id : source site identifier
    
    @retval <> NULL: pointer to the associated context
    @retval NULL: out of memory
*/
geo_srv_sync_ctx_t *geo_srv_get_eid_site_context(export_t * e,uint16_t site_id)
{
    geo_srv_sync_ctx_t *eid_site_p = NULL;
   if (EXPORT_GEO_MAX_CTX <= site_id)
   {
      severe("site is out of range %d max is %d",site_id,EXPORT_GEO_MAX_CTX);
      errno = ERANGE;
      return NULL;
   }
   
   geo_srv_sync_ctx_tab_t *tab_p = geo_srv_sync_ctx_tab_p[e->eid];
   if (geo_srv_sync_ctx_tab_p[e->eid] == NULL)
   {
     /*
     ** need to allocate a fresh entry
     */
     geo_srv_sync_ctx_tab_p[e->eid] = malloc(sizeof(geo_srv_sync_ctx_tab_t));
     if (geo_srv_sync_ctx_tab_p[e->eid]==NULL)
     {
       errno = ENOMEM;
       return NULL;
     }
     memset(geo_srv_sync_ctx_tab_p[e->eid],0,sizeof(geo_srv_sync_ctx_tab_t));
     tab_p = geo_srv_sync_ctx_tab_p[e->eid];
   }
   /*
   ** check if there is an entry for the site_id
   */
   eid_site_p = tab_p->site_table_p[site_id];
   if (tab_p->site_table_p[site_id] == NULL)
   {
     tab_p->site_table_p[site_id] = malloc(sizeof(geo_srv_sync_ctx_t));
     if (tab_p->site_table_p[site_id] ==NULL)
     {
       errno = ENOMEM;
       return NULL;
     }
     memset(tab_p->site_table_p[site_id],0,sizeof(geo_srv_sync_ctx_t));
     eid_site_p = tab_p->site_table_p[site_id];   
   }
   eid_site_p->parent.eid = e->eid;;
   eid_site_p->parent.site_id = site_id;
   strcpy(eid_site_p->parent.geo_rep_export_root_path,e->root);
   
   return eid_site_p;
}
/*
**______________________________________________________________________________
*/
/**
*  attempt to get an available entry to associate with the
   current client
   
   @param p : pointer to the eid/site_id context
   
   @retval < 0: no available entry waiting fro a client
   @retval >= 0 index of the available entry
*/
   
int geo_srv_get_eid_site_available_entry( geo_srv_sync_ctx_t *p)
{
   int i;
   
   if (p->nb_working_cur == 0) return -1;
   for (i = 0; i < GEO_MAX_SYNC_WORKING; i++)
   {
     if (p->working[i].state == GEO_WORK_AVAIL)
     {
       return i;
     }
   }
   return -1;  
}
/*
**______________________________________________________________________________
*/
/**
*  attempt to get an free entry to associate with the
   current client. The service returns the index of the free entry without
   changing its state
   
   @param p : pointer to the eid/site_id context
   
   @retval < 0: no free entry
   @retval >= 0 index of the free entry
*/
int geo_srv_get_eid_site_free_entry( geo_srv_sync_ctx_t *p)
{
   int i;
   
   for (i = 0; i < GEO_MAX_SYNC_WORKING; i++)
   {
     if (p->working[i].state == GEO_WORK_FREE)
     {
       return i;
     }
   }
   return -1;  
}

/*
**______________________________________________________________________________
*/
/**
*  Check the presence of a file_idx in the current entries 
   
   @param p : pointer to the eid/site_id context
   @param file_idx : file index to search
   
   @retval 0 : the file_idx is already in used
   @retval < 0 :not found
*/
int geo_srv_check_eid_site_file_idx( geo_srv_sync_ctx_t *p,uint64_t file_idx)
{
   int i;
   
   for (i = 0; i < GEO_MAX_SYNC_WORKING; i++)
   {
     if (p->working[i].state == GEO_WORK_FREE) continue;
     if (p->working[i].file_idx == file_idx) return 0;
   }
   return -1;  
}
/*
**______________________________________________________________________________
*/
/**
*  update the records in memory upon receiving a response from the client

   @param p : pointer to the synchro context associated with a client
   @param status_bitmap : result of the synchro : 0 OK/ 1: NOK
   @param synced_file_count: return the number of files that have been synced
*/
void geo_replicat_update_sync_file_status( geo_proc_ctx_t *p,uint64_t  status_bitmap,uint64_t *synced_file_count_p)
{
  int i;
  int nb;
  geo_fid_entry_t *rec_p;
  uint64_t synced_file_count = 0;
  *synced_file_count_p = 0;
  
  rec_p = (geo_fid_entry_t*)p->record_buf_p;
  rec_p +=p->cur_record;
  nb = p->nb_records -p->cur_record;
  if (nb > GEO_MAX_RECORDS) nb = GEO_MAX_RECORDS;
  for (i = 0; i < nb; i++,rec_p++)
  {
    if (status_bitmap & (1 << i)) continue;
    /*
    ** remove the entry since is thas been synced
    */
    rec_p->cid = 0;  
    synced_file_count++;
  }
  *synced_file_count_p = synced_file_count;
}

/*
**______________________________________________________________________________
*/
/**
*  perform the recycle of a synchro file upon close or abort

   @param p : pointer to the synchro context associated with a client
   @param geo_rep_srv_ctx_p: pointer to geo replication context associated with the eid/site
*/
int geo_replicat_recycle_geo_sync_file(geo_proc_ctx_t *p,geo_rep_srv_ctx_t *geo_rep_srv_ctx_p)
{
   int fd = -1;
   char old_path[ROZOFS_PATH_MAX];
   char new_path[ROZOFS_PATH_MAX];
   int status = -1;
   int i;
   int ret;
   geo_fid_entry_t *rec_p;
  
   rec_p = (geo_fid_entry_t*)p->record_buf_p;
   /*
   ** check if there is some files that have be synchronized. When none of
   ** them has been synchronize we leave the file on the same directory
   */
   int sync_req = 0;
   for (i = 0; i < p->nb_records;i++,rec_p++)
   {
      if (rec_p->cid == 0) {
        sync_req = 1; 
        break;
      }
   }
   if (sync_req == 0) 
   {
     if (p->recycle == 0)
     {
       /*
       ** no file synchro: if the source is the main list move to the recycle list
       */
       sprintf(old_path, "%s/%s_%d/%s_%llu", geo_rep_srv_ctx_p->geo_rep_export_root_path,GEO_DIRECTORY,
                      geo_rep_srv_ctx_p->site_id,GEO_FILE,(long long unsigned int)p->file_idx);     
       sprintf(new_path, "%s/%s_%d/%s_%llu", geo_rep_srv_ctx_p->geo_rep_export_root_path,GEO_DIR_RECYCLE,
                      geo_rep_srv_ctx_p->site_id,GEO_FILE,(long long unsigned int)p->file_idx);
       ret = rename(old_path,new_path);
       if (ret < 0)
       {
	 GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_IDX_FILE_RENAME_ERR);
	 goto error;
       }
       /**
       * update the main file index
       */
       ret = geo_rep_disk_read_index_file(geo_rep_srv_ctx_p);
       if (ret < 0)
       {
	 GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_IDX_FILE_READ_ERR);
	 goto error;
       }
       /*
       ** check if the file  matches with the index removed: in
       ** that case we update the first_index of the main index file
       */
       if (geo_rep_srv_ctx_p->geo_rep_main_file.first_index == p->file_idx)
       {
	 geo_rep_srv_ctx_p->geo_rep_main_file.first_index++;

	 ret =  geo_rep_disk_update_first_index(geo_rep_srv_ctx_p);
	 if (ret < 0) 
	 {
	   GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_IDX_FILE_WRITE_ERR);
	   goto error;
	 }  
       }     
       /**
       * update the recycle file index
       */
       ret = geo_rep_disk_read_index_file_recycle(geo_rep_srv_ctx_p);
       if (ret < 0)
       {
	 GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_IDX_FILE_READ_ERR);
	 goto error;
       }
       /*
       ** check if the file  matches with the index removed: in
       ** that case we update the first_index of the main index file
       */
       if (geo_rep_srv_ctx_p->geo_rep_main_recycle_file.last_index <= p->file_idx)
       {
	 geo_rep_srv_ctx_p->geo_rep_main_recycle_file.last_index =  p->file_idx+1;

	 ret =  geo_rep_disk_update_last_index_recycle(geo_rep_srv_ctx_p);
	 if (ret < 0) 
	 {
	   GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_IDX_FILE_WRITE_ERR);
	   goto error;
	 }  
       }
     }
     status = 0;
     goto out;
     
   }
   /*
   ** build the pathname for temp file
   */
    sprintf(old_path, "%s/%s_%d/%s_%llu", geo_rep_srv_ctx_p->geo_rep_export_root_path,GEO_DIR_RECYCLE,
                   geo_rep_srv_ctx_p->site_id,"synchro_tmp",(long long unsigned int)p->file_idx);
   /*
   ** write the temporay file
   */
   fd = open(old_path, O_RDWR | O_CREAT| O_TRUNC | O_NOATIME| O_APPEND, S_IRWXU);
   if (fd < 0)
   {
     if (errno == ENOENT) 
     {
       GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_SYNC_FILE_ENOENT);
     }
     else 
     {
       GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_SYNC_FILE_OPEN_ERR);
     }
     goto error;
   }
   /*
   ** put the date
   */
   ret = write(fd,&p->date,sizeof(p->date));
   if (ret != sizeof(p->date))
   {
     GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_IDX_FILE_WRITE_ERR);
     goto error;
   }
   /*
   ** copy the record
   */
   rec_p = (geo_fid_entry_t*)p->record_buf_p;
   for (i = 0; i < p->nb_records;i++,rec_p++)
   {
     if (rec_p->cid == 0) continue;   
     ret = write(fd,rec_p,sizeof(*rec_p));
     if (ret != sizeof(*rec_p))
     {
       GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_IDX_FILE_WRITE_ERR);
       goto error;
     }
   }   
   /*
   ** close the file
   */
   close(fd);
   fd = -1;
   
   errno = 0;
   /*
   ** now rename the file
   */
   sprintf(new_path, "%s/%s_%d/%s_%llu", geo_rep_srv_ctx_p->geo_rep_export_root_path,GEO_DIR_RECYCLE,
                  geo_rep_srv_ctx_p->site_id,GEO_FILE,(long long unsigned int)p->file_idx);
   ret = rename(old_path,new_path);
   if (ret < 0)
   {
     GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_IDX_FILE_RENAME_ERR);
     goto error;
   }
   /*
   ** remove the source file if the context is not in recycle
   */
   if (p->recycle == 0)
   {
     sprintf(old_path, "%s/%s_%d/%s_%llu", geo_rep_srv_ctx_p->geo_rep_export_root_path,GEO_DIRECTORY,
                    geo_rep_srv_ctx_p->site_id,GEO_FILE,(long long unsigned int)p->file_idx);
     unlink(old_path);    
     /**
     * update the main file index
     */
     ret = geo_rep_disk_read_index_file(geo_rep_srv_ctx_p);
     if (ret < 0)
     {
       GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_IDX_FILE_READ_ERR);
       goto error;
     }
     /*
     ** check if the file  matches with the index removed: in
     ** that case we update the first_index of the main index file
     */
     if (geo_rep_srv_ctx_p->geo_rep_main_file.first_index == p->file_idx)
     {
       geo_rep_srv_ctx_p->geo_rep_main_file.first_index++;

       ret =  geo_rep_disk_update_first_index(geo_rep_srv_ctx_p);
       if (ret < 0) 
       {
	 GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_IDX_FILE_WRITE_ERR);
	 goto error;
       }  
     }     
     /**
     * update the recycle file index
     */
     ret = geo_rep_disk_read_index_file_recycle(geo_rep_srv_ctx_p);
     if (ret < 0)
     {
       GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_IDX_FILE_READ_ERR);
       goto error;
     }
     /*
     ** check if the file  matches with the index removed: in
     ** that case we update the first_index of the main index file
     */
     if (geo_rep_srv_ctx_p->geo_rep_main_recycle_file.last_index <= p->file_idx)
     {
       geo_rep_srv_ctx_p->geo_rep_main_recycle_file.last_index =  p->file_idx+1;

       ret =  geo_rep_disk_update_last_index_recycle(geo_rep_srv_ctx_p);
       if (ret < 0) 
       {
	 GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_IDX_FILE_WRITE_ERR);
	 goto error;
       }  
     }
   }
   /*
   ** clear status to indicate that the operation is successful.
   */
   status = 0;
out:
    if (fd!= -1) close(fd);
    return status;
error:
    goto out;
}

/*
**______________________________________________________________________________
*/
/**
*  read some records from a synchro file

   @param p : pointer to the synchro context associated with a client
   @param geo_rep_srv_ctx_p: pointer to geo replication context associated with the eid/site
   @param file_idx: synchro file index
*/
int geo_replicat_read_geo_sync_file(geo_proc_ctx_t *p,geo_rep_srv_ctx_t *geo_rep_srv_ctx_p,uint64_t file_idx)
{
   int fd = -1;
   char path[ROZOFS_PATH_MAX];
   int status = -1;
   ssize_t read_len;
   size_t read_count = 0;
   struct stat buf;
  
   /*
   ** build the pathname for the current file
   */
   if (p->recycle==0)
   {
    sprintf(path, "%s/%s_%d/%s_%llu", geo_rep_srv_ctx_p->geo_rep_export_root_path,GEO_DIRECTORY,
                   geo_rep_srv_ctx_p->site_id,GEO_FILE,(long long unsigned int)file_idx);
   }
   else
   {
    sprintf(path, "%s/%s_%d/%s_%llu", geo_rep_srv_ctx_p->geo_rep_export_root_path,GEO_DIR_RECYCLE,
                   geo_rep_srv_ctx_p->site_id,GEO_FILE,(long long unsigned int)file_idx);   
   
   }
   /*
   ** get the file size in order to allocate the read buf
   */
   if (stat((const char *)path, &buf) < 0)
   {
     GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_SYNC_FILE_STAT_ERR);  
     goto error;  
   }
   p->record_buf_p = malloc(buf.st_size);
   if (p->record_buf_p == NULL)
   {
     GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_OUT_OF_MEMORY);  
     goto error;
   }
   if (buf.st_size > sizeof(uint64_t))
   {
      read_count = buf.st_size - sizeof(uint64_t);
   }   
   /*
   ** read the file
   */
   fd = open(path, O_RDWR | O_NOATIME, S_IRWXU);
   if (fd < 0)
   {
     if (errno == ENOENT) 
     {
       GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_SYNC_FILE_ENOENT);
     }
     else 
     {
       GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_SYNC_FILE_OPEN_ERR);
     }
     goto error;
   }
   errno = 0;
   /*
   ** read the file header : contains the creation time of the synchro file
   */
   read_len = pread(fd,&p->date,sizeof(p->date),0); 
   if (read_len < 0)
   {
     GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_SYNC_FILE_READ_ERR)
     goto error;
   }
   /*
   ** read the records
   */
   read_len = pread(fd,p->record_buf_p,read_count,sizeof(p->date)); 
   if (read_len < 0)
   {
     GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_SYNC_FILE_READ_ERR)
     goto error;
   }
   p->nb_records = read_len/sizeof(geo_fid_entry_t);
   p->cur_record = 0;
//   severe("--------------------------------->FDL idx file %llu nb record %d",file_idx,p->nb_records);
   if ((read_len%sizeof(geo_fid_entry_t))!= 0)
   {
     errno = EINVAL;
     GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_SYNC_FILE_READ_MISMATCH)
     goto error;
   } 
   /*
   ** clear status to indicate that the operation is successful.
   */
   status = 0;
out:
    if (fd!= -1) close(fd);
    return status;
error:
    goto out;
}
/*
**______________________________________________________________________________
*/
/**
*  read some records from the memory image of a synchro file

   @param p : pointer to the synchro context associated with a client
   @param rsp: pointer to the response context
   @param start_record : 1 to start from the beginning
*/
int geo_replicat_read_records_from_mem(geo_proc_ctx_t *p,geo_sync_data_ret_t *rsp,int start_record)
{
   ssize_t nb_records;
   geo_fid_entry_t *rec_p;
  
   rsp->eid = p->eid;
   rsp->site_id = p->site_id;
   rsp->remote_ref = p->remote_ref;
   rsp->local_ref = p->local_ref.u32;
   rsp->first_record = start_record;
   rsp->last = 0;
   rsp->file_idx = p->file_idx;   
   /*
   ** get the number of records, need to update 
   */
   if (start_record == 0)
   {
     p->cur_record += GEO_MAX_RECORDS;
     if ( p->cur_record >= p->nb_records) p->cur_record = p->nb_records;
   }     
   nb_records = p->nb_records -  p->cur_record;
   if (nb_records > GEO_MAX_RECORDS) nb_records = GEO_MAX_RECORDS; 

   if (nb_records < GEO_MAX_RECORDS) rsp->last = 1;   
   rsp->nb_records = nb_records;
   rec_p = (geo_fid_entry_t*)p->record_buf_p;
   rec_p += p->cur_record;
   rsp->data.data_val = (char *) rec_p;
   rsp->data.data_len = sizeof(geo_fid_entry_t)*nb_records;

   return 0;
}


/*
**______________________________________________________________________________
*/
/**
*  read some records from a synchro file

   @param p : pointer to the synchro context associated with a client
   @param rsp: pointer to the response context
   @param geo_rep_srv_ctx_p: pointer to geo replication context associated with the eid/site
   @param file_idx: synchro file index
   @param start_record : first record to read
*/
int geo_replicat_read_records(geo_proc_ctx_t *p,geo_sync_data_ret_t *rsp,
                              geo_rep_srv_ctx_t *geo_rep_srv_ctx_p,uint64_t file_idx,uint32_t start_record)
{
   int fd = -1;
   char path[ROZOFS_PATH_MAX];
   ssize_t nb_records;
   int status = -1;
   off_t off;
   ssize_t read_len;
  
   /*
   ** build the pathname for the current file
   */
    sprintf(path, "%s/%s_%d/%s_%llu", geo_rep_srv_ctx_p->geo_rep_export_root_path,GEO_DIRECTORY,
                   geo_rep_srv_ctx_p->site_id,GEO_FILE,(long long unsigned int)file_idx);
   rsp->eid = p->eid;
   rsp->site_id = p->site_id;
   rsp->remote_ref = p->remote_ref;
   rsp->local_ref = p->local_ref.u32;
   rsp->first_record = start_record;
   rsp->last = 0;
   rsp->data.data_val = (char *) geo_rep_bufread_p;
   rsp->file_idx = file_idx;
   /*
   ** read the file
   */
   fd = open(path, O_RDWR | O_NOATIME, S_IRWXU);
   if (fd < 0)
   {
     if (errno == ENOENT) 
     {
       GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_SYNC_FILE_ENOENT);
     }
     else 
     {
       GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_SYNC_FILE_OPEN_ERR);
     }
     goto error;
   }
   off = sizeof(geo_fid_entry_t)*start_record;
   errno = 0;
   read_len = pread(fd,geo_rep_bufread_p,GEO_MAX_RECORDS*sizeof(geo_fid_entry_t),off+GEO_REP_FILE_HDR_SZ); 
   if (read_len < 0)
   {
     GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_SYNC_FILE_READ_ERR)
     goto error;
   }
   nb_records = read_len/sizeof(geo_fid_entry_t);
   if ((read_len%sizeof(geo_fid_entry_t))!= 0)
   {
     errno = EINVAL;
     GEO_ERR_STATS(p->eid,p->site_id,GEO_ERR_SYNC_FILE_READ_MISMATCH)
     goto error;
   } 
   if (nb_records < GEO_MAX_RECORDS) rsp->last = 1;   
   rsp->nb_records = nb_records;
   rsp->data.data_len = sizeof(geo_fid_entry_t)*nb_records;
   /*
   ** clear status to indicate that the operation is successful.
   */
   status = 0;
out:
    if (fd!= -1) close(fd);
    return status;
error:
    goto out;
}

/*
**______________________________________________________________________________
*/
/**
*  get the next available file_idx to synchronise fro recycle list

   @param ctx_root_p : pointer to the eid/site_id context
   @param geo_rep_srv_ctx_p: pointer to geo replication context associated with the eid/site
   @param *file_idx : pointer to the array where the file_ix is returned
   
   @retval 0 : success, file_idx is valid
   @retval < 0 failure, file index is invalid. see errno for details
*/
int geo_replicat_get_file_idx_recycle(geo_rep_srv_ctx_t *geo_rep_srv_ctx_p,geo_srv_sync_ctx_t *ctx_root_p, uint64_t *file_idx)
{
   int ret;
   int status = -1;
   char path[ROZOFS_PATH_MAX];
   int fd = -1;
   struct stat buf;
   uint64_t idx; 
    
   *file_idx = 0;
   /*
   ** read the file index
   */
   ret = geo_rep_disk_read_index_file_recycle(geo_rep_srv_ctx_p);
   if (ret < 0)
   {
     GEO_ERR_STATS(geo_rep_srv_ctx_p->eid,geo_rep_srv_ctx_p->site_id,GEO_ERR_IDX_FILE_READ_ERR);
     goto error;
   }
   /*
   ** check if there is some file available
   */
   if (geo_rep_srv_ctx_p->geo_rep_main_recycle_first_idx == 0)
   {
     geo_rep_srv_ctx_p->geo_rep_main_recycle_first_idx = geo_rep_srv_ctx_p->geo_rep_main_recycle_file.first_index;
     if (geo_rep_srv_ctx_p->geo_rep_main_recycle_first_idx == geo_rep_srv_ctx_p->geo_rep_main_recycle_file.last_index)
     {
       errno = EAGAIN;
       goto error;      
     }
   }
   /*
   ** check if we need to wrap
   */
   if (geo_rep_srv_ctx_p->geo_rep_main_recycle_first_idx >= geo_rep_srv_ctx_p->geo_rep_main_recycle_file.last_index)
   {
     geo_rep_srv_ctx_p->geo_rep_main_recycle_first_idx = geo_rep_srv_ctx_p->geo_rep_main_recycle_file.first_index ;
   } 
   int found = 0;
   for (idx = geo_rep_srv_ctx_p->geo_rep_main_recycle_first_idx; idx < geo_rep_srv_ctx_p->geo_rep_main_recycle_file.last_index; idx++)
   {
     if (geo_srv_check_eid_site_file_idx(ctx_root_p,idx) == 0) continue;
     /*
     ** build the pathname for the current file
     */
     sprintf(path, "%s/%s_%d/%s_%llu", geo_rep_srv_ctx_p->geo_rep_export_root_path,GEO_DIR_RECYCLE,
                     geo_rep_srv_ctx_p->site_id,GEO_FILE,(long long unsigned int)idx);
     /*
     ** check the size of the file
     */
     if (stat((const char *)path, &buf) < 0)
     {
       if (errno != ENOENT) 
       {
         GEO_ERR_STATS(geo_rep_srv_ctx_p->eid,geo_rep_srv_ctx_p->site_id,GEO_ERR_SYNC_FILE_STAT_ERR);
       } 
       else
       {
          /*
	  ** update the index file if it is the first entry
	  */
	  if (idx == geo_rep_srv_ctx_p->geo_rep_main_recycle_file.first_index)
	  {
	    geo_rep_srv_ctx_p->geo_rep_main_recycle_file.first_index++;
	    geo_rep_disk_update_first_index_recycle(geo_rep_srv_ctx_p);
	  }       
       }  
       continue;  
     } 
     found = 1;
     break;
   }
   /*
   ** update the index for the next run
   */
   geo_rep_srv_ctx_p->geo_rep_main_recycle_first_idx = idx+1;
   if (found) 
   {
     *file_idx = idx;
     status = 0;
   }
out:
    if (fd!= -1) close(fd);
    return status;
error:
   goto out;
}

/*
**______________________________________________________________________________
*/
/**
*  get the next available file_idx to synchronise

   @param ctx_root_p : pointer to the eid/site_id context
   @param geo_rep_srv_ctx_p: pointer to geo replication context associated with the eid/site
   @param *file_idx : pointer to the array where the file_ix is returned
   @param *recycle : return 0 for main list and 1 for recycle list
   
   @retval 0 : success, file_idx is valid
   @retval < 0 failure, file index is invalid. see errno for details
*/
int geo_replicat_get_file_idx(geo_rep_srv_ctx_t *geo_rep_srv_ctx_p,geo_srv_sync_ctx_t *ctx_root_p, 
                              uint64_t *file_idx,uint32_t *recycle_p)
{
   int ret;
   int status = -1;
   char path[ROZOFS_PATH_MAX];
   int fd = -1;
   struct stat buf;
   uint64_t idx; 
   int recycle_checked = 0;
    
   *file_idx = 0;
   *recycle_p = 0;
   /*
   ** check if it is time to poll recycle list
   */
   if (geo_rep_srv_ctx_p->recycle_counter > geo_rep_recycle_frequency)
   {
     recycle_checked = 1;
     geo_rep_srv_ctx_p->recycle_counter = 0;
     ret = geo_replicat_get_file_idx_recycle(geo_rep_srv_ctx_p,ctx_root_p,file_idx);
     if (ret == 0)
     {
       *recycle_p = 1;
       status = 0;
       goto out;   
     }
   }
   /*
   ** case of the main file index
   */
   /*
   ** read the file index
   */
   ret = geo_rep_disk_read_index_file(geo_rep_srv_ctx_p);
   if (ret < 0)
   {
     GEO_ERR_STATS(geo_rep_srv_ctx_p->eid,geo_rep_srv_ctx_p->site_id,GEO_ERR_IDX_FILE_READ_ERR);
     goto error;
   }
   /*
   ** check if there is some file available
   */
   if (geo_rep_srv_ctx_p->geo_rep_main_file.first_index == geo_rep_srv_ctx_p->geo_rep_main_file.last_index)
   {
     /*
     ** nothing to synchronize on the main list, so check the recycle if not already done
     */
     if (recycle_checked == 0)
     {
       geo_rep_srv_ctx_p->recycle_counter = 0;
       ret = geo_replicat_get_file_idx_recycle(geo_rep_srv_ctx_p,ctx_root_p,file_idx);
       if (ret == 0)
       {
         *recycle_p = 1;
	 status = 0;
	 goto out;   
       }
     }
     goto error;      
   } 
   int found = 0;
   for (idx = geo_rep_srv_ctx_p->geo_rep_main_file.first_index; idx < geo_rep_srv_ctx_p->geo_rep_main_file.last_index; idx++)
   {
     if (geo_srv_check_eid_site_file_idx(ctx_root_p,idx) == 0) continue;
     /*
     ** build the pathname for the current file
     */
     sprintf(path, "%s/%s_%d/%s_%llu", geo_rep_srv_ctx_p->geo_rep_export_root_path,GEO_DIRECTORY,
                     geo_rep_srv_ctx_p->site_id,GEO_FILE,(long long unsigned int)idx);
     /*
     ** check the size of the file
     */
     if (stat((const char *)path, &buf) < 0)
     {
       if (errno != ENOENT) 
       {
         GEO_ERR_STATS(geo_rep_srv_ctx_p->eid,geo_rep_srv_ctx_p->site_id,GEO_ERR_SYNC_FILE_STAT_ERR);   
       }
       else
       {
          /*
	  ** update the index file if it is the first entry
	  */
	  if (idx == geo_rep_srv_ctx_p->geo_rep_main_file.first_index)
	  {
	    geo_rep_srv_ctx_p->geo_rep_main_file.first_index++;
	    geo_rep_disk_update_first_index(geo_rep_srv_ctx_p);
	  }       
       }  
       continue;  

     } 
     found = 1;
     break;
   }
   /*
   ** increment the recycle counter whatever the result is
   */
   geo_rep_srv_ctx_p->recycle_counter++;
   
   if (found) 
   {
     *file_idx = idx;
     status = 0;
   }
out:
    if (fd!= -1) close(fd);
    return status;
error:
   goto out;
}
/*
**______________________________________________________________________________
*/
/**
*   geo-replication: attempt to get some file to replicate

    @param e: pointer to the export data structure
    @param site_id : source site identifier
    @param local_ref :local reference of the caller
    @param rsp: pointer to the response context
    
   @retval 0 on success
   @retval -1 on error see errno for details 

*/
int geo_replicat_sync_req(export_t * e,uint16_t site_id,uint32_t local_ref,
                          geo_sync_data_ret_t *rsp)
{
   int status = -1;
   int entry_idx = -1;
   uint64_t file_idx;
   int ret;
   geo_proc_ctx_t *p = NULL;
   geo_srv_sync_ctx_t *ctx_root_p= NULL;
   geo_rep_srv_ctx_t *geo_rep_srv_ctx_p;
   struct timeval     timeDay;
   /*
   ** get the synchro context associated with site_id and eid
   */
   ctx_root_p = geo_srv_get_eid_site_context(e,site_id);
   if (ctx_root_p == NULL) 
   {
     /*
     **errno is asserted in case of error
     */
     GEO_ERR_STATS(e->eid,site_id,GEO_ERR_ROOT_CTX_UNAVAILABLE);
     goto error;
   }
   geo_rep_srv_ctx_p = &ctx_root_p->parent;
   /*
   ** get one available free entry
   */
   entry_idx =  geo_srv_get_eid_site_free_entry(ctx_root_p);
   if (entry_idx < 0)
   {
     /*
     ** all the entries are busy: need to try later
     */
     errno = EAGAIN;
     goto error;
   }
   /*
   ** allocate a context for the synchronisation
   */
   p = geo_proc_alloc();
   if (p== NULL)
   {
     errno = EAGAIN;
     GEO_ERR_STATS(e->eid,site_id,GEO_ERR_SYNC_CTX_UNAVAILABLE);
     goto error;      
   }  
   p->remote_ref = local_ref;  
   p->eid = e->eid;  
   p->site_id = site_id; 
   p->working_idx = entry_idx;
   p->file_idx = 0;   
   /*
   ** get one file index
   */
   ret = geo_replicat_get_file_idx(geo_rep_srv_ctx_p,ctx_root_p,&file_idx,&p->recycle);  
   if (ret < 0)
   {
      /*
      ** nothing available
      */
      errno = EAGAIN;
      goto error;
   }
   p->file_idx = file_idx;
   /*
   ** load in memory the content of the syncho file
   */
   ret = geo_replicat_read_geo_sync_file(p,geo_rep_srv_ctx_p,p->file_idx);
   if (ret < 0) goto error;
   /*
   ** get the record from the synchro file
   */
   ret = geo_replicat_read_records_from_mem(p,rsp,1);
   if (ret < 0) goto error;
   /*
   ** all is fine, set the local reference of the client synchro context and change the
   ** state of the entry
   */
   ctx_root_p->working[entry_idx].state = GEO_WORK_BUSY;
   ctx_root_p->working[entry_idx].local_ref.u32 = p->local_ref.u32;
   ctx_root_p->working[entry_idx].file_idx = file_idx;
   ctx_root_p->nb_working_cur++;
   /*
   ** start the guard timer
   */
   geo_proc_start_timer(p,geo_rep_guard_timer_ms);
   /*
   ** clear status to indicate that the operation is successful.
   */
   gettimeofday(&timeDay,(struct timezone *)0);
   p->timestamp = MICROLONG(timeDay);
   status = 0;
out:
    return status;
error:
   if (p !=NULL) geo_proc_free_from_ptr(p);
   goto out;
}
/*
**______________________________________________________________________________
*/
/**
*   geo-replication: get the next records of the file for which the client
    get a positive acknowledgement for file synchronization

    @param e: pointer to the export data structure
    @param site_id : source site identifier
    @param local_ref :local reference of the caller
    @param remote_ref :reference provided by the serveur
    @param next_record : nex record to get (first)
    @param file_idx :index of the geo-synchro file
    @param status_bitmap :64 entries: 1: failure / 0: success
    @param rsp: pointer to the response context
    
   @retval 0 on success
   @retval -1 on error see errno for details 
*/
int geo_replicat_get_next(export_t * e,uint16_t site_id,
                          uint32_t local_ref,uint32_t remote_ref,
			  uint32_t next_record,uint64_t file_idx,
			  uint64_t status_bitmap,
			  geo_sync_data_ret_t *rsp)
{
   int status = -1;
   int ret;
   geo_proc_ctx_t *p = NULL;
   geo_srv_sync_ctx_t *ctx_root_p= NULL;
   geo_rep_srv_ctx_t *geo_rep_srv_ctx_p;
   geo_local_ref_t myref;
   uint64_t synced_file_count = 0;

   /*
   ** get the synchro context from the remote_ref
   */
   myref.u32 = remote_ref;
   p = geo_proc_getObjCtx_p(myref.s.index);
   if (p == NULL)
   {
      /*
      ** context mismatch
      */
      GEO_ERR_STATS(e->eid,site_id,GEO_ERR_SYNC_CTX_ERANGE);
      errno = EINVAL;
      goto error;      
   }
   /*
   ** check if the context matches with the input paramaters
   */
   if ((p->local_ref.u32 != remote_ref) || (p->remote_ref != local_ref))
   {
     /*
     ** mismatch-> the context has been re-allocated
     */
     GEO_ERR_STATS(e->eid,site_id,GEO_ERR_SYNC_CTX_MISMATCH);
     errno = EINVAL;
     goto error;        
   }
   /*
   ** get the synchro context associated with site_id and eid
   */
   ctx_root_p = geo_srv_get_eid_site_context(e,site_id);
   if (ctx_root_p == NULL) 
   {
     /*
     **errno is asserted in case of error
     */
     GEO_ERR_STATS(e->eid,site_id,GEO_ERR_ROOT_CTX_UNAVAILABLE);
     goto error;
   }
   geo_rep_srv_ctx_p = &ctx_root_p->parent;
   /*
   ** OK, we have the right context :update the status and read the next records
   */
   geo_replicat_update_sync_file_status(p,status_bitmap,&synced_file_count);
   if (synced_file_count)
   {
    geo_rep_udpate_synced_stats(geo_rep_srv_ctx_p,synced_file_count);   
   }
   /*
   ** read the next records in sequence
   */
   ret = geo_replicat_read_records_from_mem(p,rsp,0);
   if (ret < 0) goto error;   
   status = 0;
   /*
   ** restart the guard timer
   */
   geo_proc_start_timer(p,geo_rep_guard_timer_ms);
out:
   return status;
error:
   goto out;
}

/*
**______________________________________________________________________________
*/
/**
*   geo-replication: ask for file deletion upon end of synchronization of file

    @param e: pointer to the export data structure
    @param site_id : source site identifier
    @param local_ref :local reference of the caller
    @param remote_ref :reference provided by the serveur
    @param .file_idx :index of the geo-synchro file
    
   @retval 0 on success
   @retval -1 on error see errno for details 

*/
int geo_replicat_delete(export_t * e,uint16_t site_id,
                          uint32_t local_ref,uint32_t remote_ref,
			  uint64_t file_idx)
{
   int status = -1;
   int ret;
   geo_proc_ctx_t *p = NULL;
   geo_srv_sync_ctx_t *ctx_root_p= NULL;
   geo_rep_srv_ctx_t *geo_rep_srv_ctx_p;
   geo_local_ref_t myref;
   char path[ROZOFS_PATH_MAX];
   struct timeval     timeDay;

   /*
   ** get the synchro context associated with site_id and eid
   */
   ctx_root_p = geo_srv_get_eid_site_context(e,site_id);
   if (ctx_root_p == NULL) 
   {
     /*
     **errno is asserted in case of error
     */
     GEO_ERR_STATS(e->eid,site_id,GEO_ERR_ROOT_CTX_UNAVAILABLE);
     goto error;
   }

   /*
   ** get the synchro context from the remote_ref
   */
   myref.u32 = remote_ref;
   p = geo_proc_getObjCtx_p(myref.s.index);
   if (p == NULL)
   {
      /*
      ** context mismatch
      */
      errno = EINVAL;
      GEO_ERR_STATS(e->eid,site_id,GEO_ERR_SYNC_CTX_ERANGE);
      goto error;      
   }
   geo_rep_srv_ctx_p = &ctx_root_p->parent;  

   /*
   ** check if the context matches with the input paramaters
   */
   if ((p->local_ref.u32 != remote_ref) || (p->remote_ref != local_ref))
   {
     /*
     ** mismatch-> the context has been re-allocated
     */
     errno = EINVAL;
     p = NULL;
     GEO_ERR_STATS(e->eid,site_id,GEO_ERR_SYNC_FILE_READ_MISMATCH);
     goto error;        
   }
   /**
   * update the synced file stats
   */
   geo_rep_udpate_synced_stats(geo_rep_srv_ctx_p,p->nb_records-p->cur_record);
      
   if (p->recycle == 0)
   {
     /*
     ** let's remove the synchro file
     */
     sprintf(path, "%s/%s_%d/%s_%llu", geo_rep_srv_ctx_p->geo_rep_export_root_path,GEO_DIRECTORY,
                    geo_rep_srv_ctx_p->site_id,GEO_FILE,(long long unsigned int)file_idx);
     ret = unlink(path);
     if (ret < 0) 
     {   
	GEO_ERR_STATS(e->eid,site_id,GEO_ERR_SYNC_FILE_UNLINK_ERR);
	goto error;
     }


     /**
     * update the file index
     */
     ret = geo_rep_disk_read_index_file(geo_rep_srv_ctx_p);
     if (ret < 0)
     {
       GEO_ERR_STATS(e->eid,site_id,GEO_ERR_IDX_FILE_READ_ERR);
       goto error;
     }
     /*
     ** check if the file  matches with the index removed: in
     ** that case we update the first_index of the main index file
     */
     if (geo_rep_srv_ctx_p->geo_rep_main_file.first_index == file_idx)
     {
       geo_rep_srv_ctx_p->geo_rep_main_file.first_index++;

       ret =  geo_rep_disk_update_first_index(geo_rep_srv_ctx_p);
       if (ret < 0) 
       {
	 GEO_ERR_STATS(e->eid,site_id,GEO_ERR_IDX_FILE_WRITE_ERR);
	 goto error;
       }  
     }     
     status = 0;
   }
   else
   {
     /*
     ** let's remove the synchro file from the recycle list
     */
     sprintf(path, "%s/%s_%d/%s_%llu", geo_rep_srv_ctx_p->geo_rep_export_root_path,GEO_DIR_RECYCLE,
                    geo_rep_srv_ctx_p->site_id,GEO_FILE,(long long unsigned int)file_idx);
     ret = unlink(path);
     if (ret < 0) 
     {   
	GEO_ERR_STATS(e->eid,site_id,GEO_ERR_SYNC_FILE_UNLINK_ERR);
	goto error;
     }
     /**
     * update the file index
     */
     ret = geo_rep_disk_read_index_file_recycle(geo_rep_srv_ctx_p);
     if (ret < 0)
     {
       GEO_ERR_STATS(e->eid,site_id,GEO_ERR_IDX_FILE_READ_ERR);
       goto error;
     }
     /*
     ** check if the file  matches with the index removed: in
     ** that case we update the first_index of the main index file
     */
     if (geo_rep_srv_ctx_p->geo_rep_main_recycle_file.first_index == file_idx)
     {
       geo_rep_srv_ctx_p->geo_rep_main_recycle_file.first_index++;

       ret =  geo_rep_disk_update_first_index_recycle(geo_rep_srv_ctx_p);
       if (ret < 0) 
       {
	 GEO_ERR_STATS(e->eid,site_id,GEO_ERR_IDX_FILE_WRITE_ERR);
	 goto error;
       }  
     }   
     status = 0;

   }
out:
   if (status == 0)
   {
     /*
     ** compute the synchro rate
     */
     gettimeofday(&timeDay,(struct timezone *)0);
     uint64_t timestamp = MICROLONG(timeDay);     
     if (p->timestamp < timestamp)
     {
       uint64_t result;
       result = p->nb_records;
       result = result*1000000;
       result = result/(timestamp - p->timestamp);
       //geo_rep_srv_ctx_p->synchro_rate = result;
     } 
   }
   if (p!=NULL) 
   {
     ctx_root_p->working[p->working_idx].state = GEO_WORK_FREE;
     ctx_root_p->nb_working_cur--;
     if (ctx_root_p->nb_working_cur < 0) ctx_root_p->nb_working_cur=0;
     geo_proc_free_from_ptr(p);
   }
   return status;
error:   
   goto out;
}


/*
**______________________________________________________________________________
*/
/**
*   geo-replication: ask for file abort upon end of synchro file processing

    @param e: pointer to the export data structure
    @param site_id : source site identifier
    @param local_ref :local reference of the caller
    @param remote_ref :reference provided by the serveur
    @param .file_idx :index of the geo-synchro file
    @param status_bitmap :64 entries: 1: failure / 0: success
    
   @retval 0 on success
   @retval -1 on error see errno for details 

*/
int geo_replicat_close(export_t * e,uint16_t site_id,
                          uint32_t local_ref,uint32_t remote_ref,
			  uint64_t file_idx,
			  uint64_t status_bitmap)
{
   int status = -1;
   int ret;
   geo_proc_ctx_t *p = NULL;
   geo_srv_sync_ctx_t *ctx_root_p= NULL;
   geo_rep_srv_ctx_t *geo_rep_srv_ctx_p;
   geo_local_ref_t myref;
   uint64_t synced_file_count;
   /*
   ** get the synchro context associated with site_id and eid
   */
   ctx_root_p = geo_srv_get_eid_site_context(e,site_id);
   if (ctx_root_p == NULL) 
   {
     /*
     **errno is asserted in case of error
     */
     GEO_ERR_STATS(e->eid,site_id,GEO_ERR_ROOT_CTX_UNAVAILABLE);
     goto error;
   }

   /*
   ** get the synchro context from the remote_ref
   */
   myref.u32 = remote_ref;
   p = geo_proc_getObjCtx_p(myref.s.index);
   if (p == NULL)
   {
      /*
      ** context mismatch
      */
      errno = EINVAL;
      GEO_ERR_STATS(e->eid,site_id,GEO_ERR_SYNC_CTX_ERANGE);
      goto error;      
   }
   geo_rep_srv_ctx_p = &ctx_root_p->parent;  

   /*
   ** check if the context matches with the input paramaters
   */
   if ((p->local_ref.u32 != remote_ref) || (p->remote_ref != local_ref))
   {
     /*
     ** mismatch-> the context has been re-allocated
     */
     errno = EINVAL;
     p = NULL;
     GEO_ERR_STATS(e->eid,site_id,GEO_ERR_SYNC_FILE_READ_MISMATCH);
     goto error;        
   }
   /*
   ** update the synchro file in memory
   */
   geo_replicat_update_sync_file_status(p,status_bitmap,&synced_file_count);
   if (synced_file_count)
   {
    geo_rep_udpate_synced_stats(geo_rep_srv_ctx_p,synced_file_count);   
   }
   
   /*
   ** let's rewrite the synchro file on disk
   */
   ret = geo_replicat_recycle_geo_sync_file(p,geo_rep_srv_ctx_p);
   if (ret < 0) 
   {   
      GEO_ERR_STATS(e->eid,site_id,GEO_ERR_SYNC_FILE_UNLINK_ERR);
      goto error;
   }
 
   status = 0;
out:
   if (p!=NULL)
   {
     ctx_root_p->working[p->working_idx].state = GEO_WORK_FREE;
     ctx_root_p->nb_working_cur--;
     if (ctx_root_p->nb_working_cur < 0) ctx_root_p->nb_working_cur=0;
     geo_proc_free_from_ptr(p);
   }
   return status;
error:   
   goto out;
}

/*
**______________________________________________________________________________
*/
void geo_rep_sync_rate_tmr_cbk(void *ns) 
{

    struct timeval     timeDay;
    int eid;
    int i;
    geo_srv_sync_ctx_t *root_p;
    uint64_t file_pending;
    uint64_t file_synced;
    gettimeofday(&timeDay,(struct timezone *)0);
    uint64_t timestamp = MICROLONG(timeDay);    
    uint64_t rate = 0; 
    
    for (eid=0; eid <= EXPGW_EID_MAX_IDX; eid++)
    {     
      for (i = 0; i < EXPORT_GEO_MAX_CTX; i++) 
      {
	root_p = geo_srv_get_eid_site_context_ptr(eid,i);
	if (root_p == NULL) continue;

	geo_rep_read_sync_stats(&root_p->parent,&file_pending,&file_synced);

	if (root_p->parent.last_time_watch == 0)
	{
           root_p->parent.last_time_watch = timestamp;
	   root_p->parent.last_time_sync_count = file_synced;
	}
	else
	{
	  if ( timestamp > root_p->parent.last_time_watch)
	  { 
	  rate = file_synced - root_p->parent.last_time_sync_count;
	  rate = rate*1000000;
	  rate = rate/(timestamp - root_p->parent.last_time_watch);
	  root_p->parent.synchro_rate = rate ;
	  }
	  root_p->parent.last_time_watch = timestamp;
	  root_p->parent.last_time_sync_count = file_synced;
	}   
      }     
    }
}
/*
**______________________________________________________________________________
*/
/**
  Init of the RPC server side for geo-replication
  
   @param  args: exportd start arguments
   
   @retval 0 on success
   @retval -1 on error (see errno for details)
   
*/
int geo_replicat_rpc_srv_init(void *args)
{
    int ret;
    int size;
    exportd_start_conf_param_t *args_p = (exportd_start_conf_param_t*)args;
    
    
    /*
    ** init of the rpc serveur side
    */
    ret = rozorpc_srv_module_init();
    if (ret < 0 ) return ret;
#if 0
    union {
	    geo_sync_req_arg_t geo_sync_req_1_arg;
	    geo_sync_get_next_req_arg_t geo_sync_get_next_req_1_arg;
	    geo_sync_delete_req_arg_t geo_sync_delete_req_1_arg;
    } argument;
    
    size = sizeof(argument);
    decoded_rpc_buffer_pool = ruc_buf_poolCreate(ROZORPC_SRV_CTX_CNT,size);
    if (decoded_rpc_buffer_pool == NULL) {
      fatal("Can not allocate decoded_rpc_buffer_pool");
      return -1;
    }
#endif 
    /*
    ** allocate a buffer for reading file
    */
    geo_rep_bufread_p = malloc(sizeof(geo_fid_entry_t)*GEO_MAX_RECORDS);
    if (geo_rep_bufread_p == NULL)
    {
      fatal("out of memory");
      return -1;    
    }
    /*
    ** Init of the north interface (read/write request processing)
    */ 
    ret = geo_replicat_north_interface_buffer_init(GEO_REPLICA_BUF_RECV_CNT, GEO_REPLICA_BUF_RECV_SZ);
    if (ret < 0) {
      fatal("Fatal error on storio_north_interface_buffer_init()\n");
      return -1;
    }
    
    ret = geo_replicat_north_interface_init(args_p->exportd_hostname,GEO_REPLICA_SLAVE_PORT);
    if (ret < 0) {
      fatal("Fatal error on geo_replicat_north_interface_init()\n");
      return -1;
    }
    geo_rep_ctx_tmr.p_periodic_timCell=ruc_timer_alloc(0,0);
    if (geo_rep_ctx_tmr.p_periodic_timCell == (struct timer_cell *)NULL){
        severe( "No timer available for MS timer periodic" );
        return-1;
    }
    geo_rep_ctx_tmr.period_ms = 10000;
    geo_rep_ctx_tmr.credit = 1;
    ruc_periodic_timer_start(geo_rep_ctx_tmr.p_periodic_timCell,
	      (geo_rep_ctx_tmr.period_ms*TIMER_TICK_VALUE_100MS/100),
	      &geo_rep_sync_rate_tmr_cbk,
	      0);
	      
    
    /*
    ** enable the log by default
    */
    memset(geo_srv_sync_err_tab_p,0,((EXPGW_EID_MAX_IDX+1)*EXPORT_GEO_MAX_CTX)*sizeof(geo_srv_sync_err_t));
    geo_replica_log_enable = 0;
    geo_rep_recycle_frequency = GEO_REP_RECYCLE_FREQ;
    /*
    **
    */ 
    uma_dbg_addTopic("geo_error", show_geo_err);
    uma_dbg_addTopic("geo_statistics", show_geo_rep_stats);
    uma_dbg_addTopic("geo_files", show_geo_rep_files);
    return 0;

}
