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

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <rozofs/common/log.h>
#include <rozofs/core/uma_dbg_api.h>

#include "geo_replication.h"
#include "export.h"
#define GEO_REPLICATION_MAX_EID 257


geo_rep_srv_ctx_t *geo_rep_srv_tb0[GEO_REPLICATION_MAX_EID];
geo_rep_srv_ctx_t *geo_rep_srv_tb1[GEO_REPLICATION_MAX_EID];
int geo_rep_init_done = 0;

/*
**____________________________________________________________________________
*/
void export_geo_rep_reset_one(uint32_t eid)
{
    geo_rep_srv_ctx_t * prof;   

    if (eid>=GEO_REPLICATION_MAX_EID) return ;
    
    prof = geo_rep_srv_tb0[eid];
    if (prof != NULL) 
    {    
      memset(&prof->stats,0,sizeof(geo_rep_stats_t));
    }
    prof = geo_rep_srv_tb1[eid];
    if (prof == NULL) return;    
    memset(&prof->stats,0,sizeof(geo_rep_stats_t));
}
/*
**____________________________________________________________________________
*/

void export_geo_rep_reset_all()
{
  int i;
  
  for(i = 0; i < GEO_REPLICATION_MAX_EID; i++)
  {
    export_geo_rep_reset_one(i);
  }
}
/*
**____________________________________________________________________________
*/
#define GEOREP_CTX_STATS(name,val) \
  pChar +=  sprintf(pChar,"%12s | %24llu | %24llu |\n",name,(unsigned long long int)prof->stats.val,(unsigned long long int)prof1->stats.val)
char * show_geo_rep_one(char * pChar, uint32_t eid) {
    geo_rep_srv_ctx_t * prof,*prof1;
    char bufall1[64];   
    char bufall0[64];   

    if (eid>=GEO_REPLICATION_MAX_EID) return pChar;
    if (exportd_is_master()== 1) return pChar;   
    if (exportd_is_eid_match_with_instance(eid) ==0) return pChar;
       
    
    prof = geo_rep_srv_tb0[eid];
    prof1 = geo_rep_srv_tb1[eid];
    if (prof == NULL) return pChar;
    
     pChar +=  sprintf(pChar, "eid                : %d\n",prof->eid);    
     pChar +=  sprintf(pChar, "path               : %s\n",prof->geo_rep_export_root_path);    
     pChar +=  sprintf(pChar, "max. file size     : %llu Bytes\n",(unsigned long long int)prof->max_filesize);    
     pChar +=  sprintf(pChar, "flush delay        : %llu seconds\n",(unsigned long long int)prof->delay);    
     pChar +=  sprintf(pChar, "availability delay : %llu seconds\n",(unsigned long long int)prof->delay_next_file);    
     sprintf(bufall0, "%llu/%llu",(unsigned long long int)prof->geo_rep_main_file.first_index,
                                              (unsigned long long int)prof->geo_rep_main_file.last_index);    
     sprintf(bufall1, "%llu/%llu",(unsigned long long int)prof1->geo_rep_main_file.first_index,
                                              (unsigned long long int)prof1->geo_rep_main_file.last_index);    
    
    pChar +=  sprintf(pChar, "statistics : \n");   
    pChar +=  sprintf(pChar,"%12s | %24s | %24s |\n","","site 0" ,"site 1");
    pChar +=  sprintf(pChar,"-------------+--------------------------+--------------------------+\n");
    pChar +=  sprintf(pChar,"%12s | %24s | %24s |\n","file idx",bufall0,bufall1);
    GEOREP_CTX_STATS("insertions",insert_count);
    GEOREP_CTX_STATS("updates",update_count);
    GEOREP_CTX_STATS("deletion",delete_count);
    GEOREP_CTX_STATS("collisions",coll_count);
    GEOREP_CTX_STATS("flush",flush_count);
    GEOREP_CTX_STATS("stat err",stat_err);
    GEOREP_CTX_STATS("open err",open_err);
    GEOREP_CTX_STATS("access err",access_err);
    GEOREP_CTX_STATS("write err",write_err);

     return pChar;  
}
/*
**____________________________________________________________________________
*/
static char * show_geo_replication_help(char * pChar) {
  pChar += sprintf(pChar,"usage:\n");
  pChar += sprintf(pChar,"geo-replica reset [ <eid> ] : reset statistics\n");
  pChar += sprintf(pChar,"geo-replica [ <eid> ]       : display statistics\n");  
  return pChar; 
}
/*
**____________________________________________________________________________
*/
void show_geo_replication(char * argv[], uint32_t tcpRef, void *bufRef) {
    char *pChar = uma_dbg_get_buffer();
    uint32_t eid;
    int ret;

    if (geo_rep_init_done == 0) 
    {
      /*
      ** service is not available
      */
      sprintf(pChar,"service not available\n");
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());  
      return; 	  

    }

    if (argv[1] == NULL) {
      pChar += sprintf(pChar,"Exportd local site %d\n",export_get_local_site_number());
      for (eid=0; eid <= EXPGW_EID_MAX_IDX; eid++) 
        pChar = show_geo_rep_one(pChar,eid);
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   	  
      return;
    }

    if (strcmp(argv[1],"reset")==0) {

      if (argv[2] == NULL) {
	export_geo_rep_reset_all();
	uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   
	return;	 
      }

      ret = sscanf(argv[2], "%d", &eid);
      if (ret != 1) {
        show_geo_replication_help(pChar);	
	uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
	return;   
      }
      export_geo_rep_reset_one(eid);
      uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   	  
      return;
    }
    ret = sscanf(argv[1], "%d", &eid);
    if (ret != 1) {
      show_geo_replication_help(pChar);	
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;   
    }
    pChar += sprintf(pChar,"Exportd local site %d\n",export_get_local_site_number());
    pChar = show_geo_rep_one(pChar,eid);
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   	  
    return;
}

/*
**____________________________________________________________________________
*/
/**
*  init of the debug function
*/
int geo_rep_dbg_init()
{
   
   int i;
   for (i = 0; i < GEO_REPLICATION_MAX_EID; i++) 
   {
     geo_rep_srv_tb0[i] = NULL;
     geo_rep_srv_tb1[i] = NULL;
   }
//    uma_dbg_addTopic("geo-replica", show_geo_replication);
    geo_rep_init_done = 1;
    return 0;

}



/*
**____________________________________________________________________________
*/
/**
*  add a context in the debug table

   @param p : ocntext to add (the key is the value of the eid
   
   @retval 0 on success
   @retval -1 on error
*/
int geo_rep_dbg_add(geo_rep_srv_ctx_t *p)
{

   if (geo_rep_init_done == 0)
   {
     geo_rep_dbg_init();
   }
   if (p->eid >= GEO_REPLICATION_MAX_EID)
   {
      errno = EINVAL;
      return -1;
   }
   if (p->site_id == 0)
   {
     geo_rep_srv_tb0[p->eid] = p;
   }
   else
   {
     geo_rep_srv_tb1[p->eid] = p;   
   }
   return 0;
}


