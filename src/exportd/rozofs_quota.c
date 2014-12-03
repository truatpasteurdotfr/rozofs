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

#include <string.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <rozofs/rozofs.h>
#include <rozofs/common/htable.h>
#include <rozofs/common/log.h>
#include "rozofs_quota.h"
#include "rozofs_quota_api.h"
#include <rozofs/core/disk_table_service.h>
#include <rozofs/common/types.h>
#include <rozofs/core/uma_dbg_api.h>
 
/*
** quota file default names
*/ 
char *quotatypes[] = INITQFNAMES;

rozofs_qt_export_t *export_quota_table[EXPGW_EID_MAX_IDX+1] = {0};
int rozofs_qt_init_done = 0;
rozofs_qt_cache_t rozofs_qt_cache;  /**< quota cache */

/*
 *_______________________________________________________________________
 */
char * rozofs_qt_cache_display(rozofs_qt_cache_t *cache, char * pChar) {

  pChar += sprintf(pChar, "lv2 attributes cache : current/max %u/%u\n",cache->size, cache->max);
  pChar += sprintf(pChar, "hit %llu / miss %llu / lru_del %llu\n",
                   (long long unsigned int) cache->hit, 
		   (long long unsigned int)cache->miss,
		   (long long unsigned int)cache->lru_del);
  pChar += sprintf(pChar, "entry size %u - current size %u - maximum size %u\n", 
                   (unsigned int) sizeof(rozofs_qt_cache_entry_t), 
		   (unsigned int)sizeof(rozofs_qt_cache_entry_t)*cache->size, 
		   (unsigned int)sizeof(rozofs_qt_cache_entry_t)*cache->max); 
  return pChar;		   
}

/*
 *_______________________________________________________________________
 */
/**
*   Quota cache statistics

  @param argv : standard argv[] params of debug callback
  @param tcpRef : reference of the TCP debug connection
  @param bufRef : reference of an output buffer 
  
  @retval none
*/
void show_quota_cache(char * argv[], uint32_t tcpRef, void *bufRef) {
  rozofs_qt_cache_display( &rozofs_qt_cache, uma_dbg_get_buffer());
  uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
}


static char * rw_quota_help(char * pChar) {
  pChar += sprintf(pChar,"usage:\n");
  pChar += sprintf(pChar,"quota_get eid <eid> [group|user] <value>: get user or group quota within an eid\n");
  return pChar; 
}

/*
 *_______________________________________________________________________
 */
/**
*   Read quota user or group

  @param argv : standard argv[] params of debug callback
  @param tcpRef : reference of the TCP debug connection
  @param bufRef : reference of an output buffer 
  
  @retval none
*/
void rw_quota_entry(char * argv[], uint32_t tcpRef, void *bufRef) {
  
    char *pChar = uma_dbg_get_buffer();
    int eid;
    int type;
    int qid;
    rozofs_qt_cache_entry_t *dquot=NULL;
    rozofs_quota_key_t key;
    rozofs_qt_export_t *p;
    int ret;

      if (argv[1] == NULL) {
	rw_quota_help(pChar);	
	uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;  	  
    }

    if (strcmp(argv[1],"eid")==0) 
    {   
      if (argv[2] == NULL) 
      {
	rw_quota_help(pChar);	
	uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
	return;  	  
      }
      ret = sscanf(argv[2], "%d", &eid);
      if (ret != 1) 
      {
        rw_quota_help(pChar);	
        uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
        return;   
      }
    }
    else
    {
      rw_quota_help(pChar);	
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;       
    }
    while(1)
    {
      if (strcmp(argv[3],"user")==0) {   
           type = USRQUOTA;
	   break;
      }
      if (strcmp(argv[3],"group")==0) {   
           type = GRPQUOTA;
	   break;
      }      
      rw_quota_help(pChar);	
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;  
    }
    /*
    ** check the id
    */
    if (argv[4] == NULL) {
      rw_quota_help(pChar);	
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;  	  
    }
    ret = sscanf(argv[4], "%d", &qid);
    if (ret != 1) {
      rw_quota_help(pChar);	
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;   
    }
    /*
    ** check the presence of the exportd
    */
    if (rozofs_qt_init_done == 0)
    {
      sprintf(pChar,"Tne Quota module is not active\n");
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;
    }
    if (eid > EXPGW_EID_MAX_IDX)
    {
      sprintf(pChar,"Tne EID is out of range (max is %u)\n",EXPGW_EID_MAX_IDX);
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;
    }
    if (export_quota_table[eid]== NULL)
    {
      sprintf(pChar,"Quota does not exist for EID %d\n",eid);
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;
    }
    /*
    ** attempt to read the quota
    */
    p = export_quota_table[eid];
    key.u64 = 0;
    key.s.qid = qid;
    key.s.eid = eid;
    key.s.type = type;    
    dquot = rozofs_qt_cache_get (&rozofs_qt_cache,p->quota_inode[type],&key);
    if (dquot == NULL)
    {
      sprintf(pChar,"Cannot read for the requested id\n");
    }
    else
    {
      pChar +=sprintf(pChar,"Displaying quota for %s %u of exportd %d\n",(type==USRQUOTA)?"User":"Group",qid,eid);
      rozofs_qt_print(pChar,&dquot->dquot);
    }
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
}

/**
 * hashing function used to find lv2 entry in the cache
 */
static inline uint32_t rozofs_qt_hash(void *key) {
    uint32_t hash = 0;
    uint8_t *c;

    for (c = key; c != key + sizeof(rozofs_quota_key_t); c++)
        hash = *c + (hash << 6) + (hash << 16) - hash;
    return hash;
}

static inline int rozofs_qt_cmp(void *k1, void *k2) {
    
    return memcmp((const void*)k1,(const void *)k2,sizeof(rozofs_quota_key_t));
}

/*
 *___________________________________________________________________
 * Put the entry in front of the lru list when no lock is set
 *
 * @param cache: the cache context
 * @param entry: the cache entry
 *___________________________________________________________________
 */
static inline void rozofs_qt_cache_update_lru(rozofs_qt_cache_t *cache, rozofs_qt_cache_entry_t *entry) {
    list_remove(&entry->list);
    list_push_front(&cache->lru, &entry->list);
}
/*
**__________________________________________________________________
*/
/**
*   Remove an entry from the quota cache

    @param: pointer to the cache context
    @param: pointer to entry to remove
    
    @retval none
*/
static inline void rozofs_qt_cache_unlink(rozofs_qt_cache_t *cache,rozofs_qt_cache_entry_t *entry) {

  list_remove(&entry->list);
  free(entry);
  cache->size--;  
}

/*
**__________________________________________________________________
*/
/**
*   allocate a quota cache entry

    @param: pointer to the cache context
    @param: pointer to entry to remove
    
    @retval none
*/
rozofs_qt_cache_entry_t *rozofs_qt_cache_entry_alloc() 
{

    rozofs_qt_cache_entry_t *entry=NULL;
    
    entry = malloc(sizeof(rozofs_qt_cache_entry_t));
    if (entry == NULL)
    {
      /*
      ** out of memory
      */
      severe("Out of memory");
      return NULL;       
    }  
    memset(entry,0,sizeof(rozofs_qt_cache_entry_t));
    list_init(&entry->list);
    return entry;
}

/*
**__________________________________________________________________
*/
/**
*   init of a QUOTA cache

    @param: pointer to the cache context
    
    @retval none
*/
void rozofs_qt_cache_initialize(rozofs_qt_cache_t *cache) {
    cache->max = ROZOFS_QT_MAX_ENTRIES;
    cache->size = 0;
    cache->hit  = 0;
    cache->miss = 0;
    cache->lru_del = 0;
    list_init(&cache->lru);
    htable_initialize(&cache->htable, ROZOFS_QT_BUKETS, rozofs_qt_hash, rozofs_qt_cmp);
}

/*
**__________________________________________________________________
*/
/**
*   delete of an exportd attribute cache

    @param: pointer to the cache context
    
    @retval none
*/
void rozofs_qt_cache_release(rozofs_qt_cache_t *cache) {
    list_t *p, *q;

    list_for_each_forward_safe(p, q, &cache->lru) {
        rozofs_qt_cache_entry_t *entry = list_entry(p, rozofs_qt_cache_entry_t, list);
        htable_del(&cache->htable, &entry->dquot.key.u64);
	rozofs_qt_cache_unlink(cache,entry);
    }
}

/*
**__________________________________________________________________
*/
/**
*   Get an enry from the quota cache

    @param: pointer to the cache context
    @param disk_p: quota disk table associated with the type
    @param: key : the key contains the eid,type and quota identifier within the type
    
    @retval <>NULL : pointer to the cache entry that contains the quota
    @retval NULL: not found
*/
rozofs_qt_cache_entry_t *rozofs_qt_cache_get(rozofs_qt_cache_t *cache,
                                             disk_table_header_t *disk_p,
					     rozofs_quota_key_t *key) 
{
    rozofs_qt_cache_entry_t *entry = 0;
    int ret;


    if ((entry = htable_get(&cache->htable, key)) != 0) {
        // Update the lru
        rozofs_qt_cache_update_lru(cache,entry); 
	cache->hit++;
    }
    else 
    {
       entry = rozofs_qt_cache_entry_alloc();
       if (entry == NULL)
       {
	 return NULL;       
       }
       entry->dquot.key.u64 = key->u64;
      ret = quota_wbcache_read(disk_p,&entry->dquot,sizeof(rozofs_dquot_t));
      if (ret < 0)
      {
	 /*
	 ** warn but keep the data in cache
	 */
	 severe("quota writeback cache write failure");
	 free(entry);
      }
      cache->miss++;
    }
    return entry;
}

/*
**__________________________________________________________________
*/
/**
*   The purpose of that service is to read object attributes and store them in the attributes cache

  @param cache : pointer to the export attributes cache
  @param disk_p: quota disk table associated with the type
  @param entry:pointer to the entry to insert (the key must be provisioned)
  
  @retval <> NULL: attributes of the object
  @retval == NULL : no attribute returned for the object (see errno for details)
*/

rozofs_qt_cache_entry_t *rozofs_qt_cache_put(rozofs_qt_cache_t *cache,
                                             disk_table_header_t *disk_p,
                                             rozofs_qt_cache_entry_t *entry) 
{
    int count=0;
    int ret;
    /*
    ** init of the linked list
    */
    list_remove(&entry->list);

    /*
    ** Try to remove older entries
    */
    while ((cache->size >= cache->max) && (!list_empty(&cache->lru))){ 
      rozofs_qt_cache_entry_t *lru;
		
	  lru = list_entry(cache->lru.prev, rozofs_qt_cache_entry_t, list);  
	  htable_del(&cache->htable, &lru->dquot.key.u64);
	  rozofs_qt_cache_unlink(cache,lru);
	  cache->lru_del++;
	  count++;
	  if (count >= 3) break;
    }
    /*
    ** Insert the new entry
    */
    rozofs_qt_cache_update_lru(cache,entry);
    htable_put(&cache->htable, &entry->dquot.key.u64, entry);
    cache->size++;    
    /*
    ** push the data in the quota write back cache
    */
    ret = quota_wbcache_write(disk_p,&entry->dquot,sizeof(rozofs_dquot_t));
    if (ret < 0)
    {
       /*
       ** warn but keep the data in cache
       */
       severe("quota writeback cache write failure");
    }
    return entry;
}


/*
**__________________________________________________________________
*/
/**
*  Export quota table create

   That service is called at export creation time. Its purpose is to allocate
   data structure for export attributes management.
   
   @param eid : export identifier
   @param root_path : root path of the export
   @param create_flag : assert to 1 if quota files MUST be created
   
   @retval <> NULL: pointer to the attributes tracking table
   @retval == NULL : error (see errno for details)
*/
void *rozofs_qt_alloc_context(uint16_t eid, char *root_path, int create)
{
   rozofs_qt_export_t *tab_p = NULL; 
   int ret; 
   int i;
   
   /*
   ** if the init of the quota module has not yet been done do it now
   */

   if (rozofs_qt_init_done == 0)
   {
     ret = rozofs_qt_init();
     if (ret < 0) return NULL;
   }
   /*
   ** check if the entry already exists: this is done to address the case of the exportd reload
   */
   if (eid > EXPGW_EID_MAX_IDX) 
   {
      /*
      ** eid value is out of range
      */
      severe("failed to create ressource: eid %d is out of range max is %d",eid,EXPGW_EID_MAX_IDX);
      return NULL;
   }
   if (export_quota_table[eid]!= NULL)
   {
      /*
      ** the context is already allocated: nothing more to done:
      **  note: it is not foreseen the change the root path of an exportd !!
      */
      return (void*)export_quota_table[eid];
   }
   
   tab_p = malloc(sizeof(rozofs_qt_export_t));
   if (tab_p == NULL)
   {
     /*
     ** out of memory
     */
     return NULL;
   }
   memset(tab_p,0,sizeof(rozofs_qt_export_t));
   /*
   ** duplicate the root path
   */
   tab_p->root_path = strdup(root_path);
   if (tab_p->root_path == NULL)
   {
     severe("out of memory");
     goto error;
   }
   /*
   ** set the limit in quota_info
   */
   for (i = 0; i < MAXQUOTAS; i++)
   {
      tab_p->quota_super[i].dqi_maxblimit = 0xffffffffffffffffULL;
      tab_p->quota_super[i].dqi_maxilimit = 0xffffffffffffffffULL;
      tab_p->quota_super[i].dqi_bgrace = ROZOFS_MAX_DQ_TIME;
      tab_p->quota_super[i].dqi_igrace = ROZOFS_MAX_IQ_TIME;
      tab_p->quota_super[i].enable = 1;
   }
   for (i = 0; i < MAXQUOTAS; i++)
   {
     tab_p->quota_inode[i] = disk_tb_ctx_allocate(root_path,quotatypes[i],sizeof(rozofs_dquot_t),ROZOFS_QUOTA_DISK_TB_SZ_POWER2);
     if (tab_p->quota_inode[i] == NULL) goto error;

   }
   /*
   ** everything is fine, so store the reference of the context in the table at the index of the eid
   */
   export_quota_table[eid]= tab_p;
   
   return (void*)tab_p;

error:
#warning    rozofs_qt_release_context(tab_p) not yet implemented
   return NULL;
}

/*
**__________________________________________________________________
*/

static inline int rozofs_qt_dqot_inode_update(disk_table_header_t *disk_p,int eid,int type,int qid,uint64_t count,int action)
{
    rozofs_qt_cache_entry_t *dquot;
    rozofs_quota_key_t key;
    
    key.u64 = 0;
    key.s.qid = qid;
    key.s.eid = eid;
    key.s.type = type;
    
    int status = -1;
    while (1)
    {
      dquot = rozofs_qt_cache_get (&rozofs_qt_cache,disk_p,&key);
      if (dquot == NULL)
      {
        /*
	** should not happen
	*/
        break;
      }
      if (action == ROZOFS_QT_INC)
      {
        dquot->dquot.quota.dqb_curinodes +=count; 
      }  
      else
      {
        dquot->dquot.quota.dqb_curinodes -=count; 
	if (dquot->dquot.quota.dqb_curinodes<0)
	{
	  dquot->dquot.quota.dqb_curinodes =0; 
	} 
      } 
      /*
      ** let's write qota on disk
      */ 
      rozofs_qt_cache_put(&rozofs_qt_cache,disk_p,dquot);  
      status = 0;
      break; 
    } 
    return status;
}
/*
**__________________________________________________________________
*/
/**
*   update inode quota
    
    @param eid: export identifier
    
    @param usr_id : user quota
    @param grp_id : group quota
    @param nb_inode
    @param action: 1: increment, 0 decrement 
    
    @retval : 0 on success
    @retval < 0 on error
 */
int rozofs_qt_inode_update(int eid,int user_id,int grp_id,int nb_inode,int action)
{
   rozofs_qt_export_t *p;
   /*
   ** get the pointer to the quota context associated with the eid
   */
   /*
   ** check if the entry already exists: this is done to address the case of the exportd reload
   */
   if (eid > EXPGW_EID_MAX_IDX) 
   {
      /*
      ** eid value is out of range
      */
      return  -1;
   }
   if (export_quota_table[eid]== NULL)
   {
      return -1;
   }
   p = export_quota_table[eid];
   /*
   ** update user account
   */
   if (user_id != -1)
   {
      rozofs_qt_dqot_inode_update(p->quota_inode[USRQUOTA],eid,USRQUOTA,user_id,nb_inode,action);
   }
   if (grp_id != -1)
   {
      rozofs_qt_dqot_inode_update(p->quota_inode[GRPQUOTA],eid,GRPQUOTA,user_id,nb_inode,action);
   }
   return 0;

}

/*
**__________________________________________________________________
*/

static inline int rozofs_qt_dqot_block_update(disk_table_header_t *disk_p,int eid,int type,int qid,uint64_t count,int action)
{
    rozofs_qt_cache_entry_t *dquot;
    rozofs_quota_key_t key;
    
    key.u64 = 0;
    key.s.qid = qid;
    key.s.eid = eid;
    key.s.type = type;
    
    int status = -1;
    while (1)
    {
      dquot = rozofs_qt_cache_get (&rozofs_qt_cache,disk_p,&key);
      if (dquot == NULL)
      {
        /*
	** should not happen
	*/
        break;
      }
      if (action == ROZOFS_QT_INC)
      {
        dquot->dquot.quota.dqb_curspace +=count; 
      }  
      else
      {
        dquot->dquot.quota.dqb_curspace -=count; 
	if (dquot->dquot.quota.dqb_curspace<0)
	{
	  dquot->dquot.quota.dqb_curspace =0; 
	} 
      } 
      /*
      ** let's write qota on disk
      */ 
      rozofs_qt_cache_put(&rozofs_qt_cache,disk_p,dquot);  
      status = 0;
      break; 
    } 
    return status;
}
/*
**__________________________________________________________________
*/
/**
*   update size (blocks) quota
    
    @param eid: export identifier
    
    @param usr_id : user quota
    @param grp_id : group quota
    @param nb_inode
    @param action: 1: increment, 0 decrement 
    
    @retval : 0 on success
    @retval < 0 on error
 */
int rozofs_qt_block_update(int eid,int user_id,int grp_id,uint64_t size,int action)
{
   rozofs_qt_export_t *p;
   /*
   ** get the pointer to the quota context associated with the eid
   */
   
   /*
   ** check if the entry already exists: this is done to address the case of the exportd reload
   */
   if (eid > EXPGW_EID_MAX_IDX) 
   {
      /*
      ** eid value is out of range
      */
      return  -1;
   }
   if (export_quota_table[eid]== NULL)
   {
      return -1;
   }
   p = export_quota_table[eid];
   /*
   ** update user account
   */
   if (user_id != -1)
   {
      rozofs_qt_dqot_block_update(p->quota_inode[USRQUOTA],eid,USRQUOTA,user_id,size,action);
   }
   if (grp_id != -1)
   {
      rozofs_qt_dqot_block_update(p->quota_inode[GRPQUOTA],eid,GRPQUOTA,user_id,size,action);
   }
   return 0;

}

/*
**__________________________________________________________________
*/
/**
*   Init of the quota module of RozoFS

    @param none
    
    @retval 0 on success
    @retval -1 on error
*/
int rozofs_qt_init()
{
    int ret;

    if (rozofs_qt_init_done == 1) return 0;
    /*
    ** allocate the cache
    */
    rozofs_qt_cache_initialize(&rozofs_qt_cache);
    
    /*
    ** init of the writeback cache+ thread
    */
    ret = quota_wbcache_init();
    if (ret < 0)
    {
      severe("error on writeback quota cache init\n");
      return -1;
    }
    rozofs_qt_init_done = 1;
  return 0;    
}
