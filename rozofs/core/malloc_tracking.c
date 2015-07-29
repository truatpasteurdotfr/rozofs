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

#include <malloc.h>
#include <rozofs/common/list.h>
#include <rozofs/core/uma_dbg_api.h>

static void * malloc_tracking_hook (size_t size, const void *caller);
static void   free_tracking_hook (void *ptr, const void *caller);
static void * realloc_tracking_hook (void *ptr, size_t size, const void *caller);
static void * memalign_tracking_hook (size_t alignment, size_t size, const void *caller);

typedef struct _malloc_tracking_t
{ 
   int                       size;
   uint16_t                  hash;
   void                    * addr;
   const void              * caller;
   uint64_t                  ts;
   list_t                    list;
} malloc_tracking_t;

static uint64_t malloc_tracking_entries=0;
static int      malloc_tracking_status=0;
static int      malloc_tracking_trace=0;

#define MALLOC_TRACKING_HASH_ENTRIES 4096

malloc_tracking_t   malloc_tracking_table[MALLOC_TRACKING_HASH_ENTRIES];

static malloc_tracking_t * dbg_current_trk = NULL;


void *(*legacy_malloc_hook)(size_t, const void *);
void (*legacy_free_hook)(void *ptr, const void *caller);
void *(*legacy_realloc_hook) (void *ptr, size_t size, const void *caller);
void *(*legacy_memalign_hook) (size_t alignment, size_t size, const void *caller);

/*
**_______________________________________________________
** Install my own hooks instead of legacy hooks
*/
static inline void install_my_hooks(void) {
  __malloc_hook   = malloc_tracking_hook;
  __free_hook     = free_tracking_hook;  
  __realloc_hook  = realloc_tracking_hook;  
  __memalign_hook = memalign_tracking_hook;  

}
/*
**_______________________________________________________
** Restore the legacy hooks
*/
static inline void restore_legacy_hooks(void) {
  __malloc_hook   = legacy_malloc_hook;
  __free_hook     = legacy_free_hook;
  __realloc_hook  = legacy_realloc_hook;    
  __memalign_hook = legacy_memalign_hook;  
}
/*
**_______________________________________________________
** Save the legacy hooks
*/
static inline void save_legacy_hooks(void) {  
  legacy_malloc_hook   = __malloc_hook;
  legacy_free_hook     = __free_hook;
  legacy_realloc_hook  = __realloc_hook;
  legacy_memalign_hook = __memalign_hook;  
}
/*
**_______________________________________________________
** Compute a shas value from an address
*/
static uint16_t malloc_hash (void * ptr) {
  int i;
  uint32_t h = 2166136261U;
  uint64_t u64 = (uint64_t) ptr;
  
  for (i=0; i < 8; i++) {
    h = (h * 16777619) ^ ((u64>>8)&0xFF);
  }
  return h%MALLOC_TRACKING_HASH_ENTRIES;  
}
/*
**_______________________________________________________
** Lookup a malloc tracking entry from the pointer address
**
** @param ptr   The address of the tracked memory area
**
** @retval the tracking context or NULL
*/
static inline malloc_tracking_t * find_malloc_entry(void * ptr) {
  uint16_t            hash;
  list_t            * p=NULL;
  malloc_tracking_t * trk;
  
  if (ptr == NULL) return NULL;

  hash = malloc_hash(ptr);

  list_for_each_forward(p, &malloc_tracking_table[hash].list) {
  
    trk = list_entry(p, malloc_tracking_t, list);

    if (trk->addr == ptr) {
      return trk;
    }  
  }
  return NULL;
}
/*
**_______________________________________________________
** Create a new malloc tracking entry 
**
** @param ptr     The address of the tracked memory area
** @param size    The size of the tracked memory area
** @param caller  The scaller PC
*/
static inline void new_malloc_entry(void * ptr, size_t size, const void *caller) {
  uint16_t            hash;
  malloc_tracking_t * trk;

  if (ptr == NULL) return;
  
  hash = malloc_hash(ptr);
  
  malloc_tracking_entries++;
    
  /* Allocate also a malloc trackinking structure */
  trk = malloc(sizeof(malloc_tracking_t));  

  trk->size   = size;
  trk->hash   = hash;
  trk->addr   = ptr;
  trk->caller = caller;
  list_init(&trk->list);
  trk->ts = time(NULL); 
  
  list_push_front(&malloc_tracking_table[hash].list,&trk->list);
  malloc_tracking_table[hash].size++;

 
}
/*
**_______________________________________________________
** Release a malloc tracking entry 
**
** @param trk     The malloc tracking context
*/
static inline void release_malloc_entry(malloc_tracking_t * trk) {
  
  malloc_tracking_table[trk->hash].size--;
  malloc_tracking_entries--;
    
  /*
  ** This is the cirrent debug tracking context.
  ** get the next context
  */
    
  if (trk == dbg_current_trk) {
    int curr_bucket = dbg_current_trk->hash;
    /*
    ** Get next tracking context
    */
    list_t * p = dbg_current_trk->list.next;
    dbg_current_trk = list_entry(p, malloc_tracking_t, list);
    
    /*
    ** Last tracking context in the bucket
    */
    if (dbg_current_trk == &malloc_tracking_table[curr_bucket]) {
      curr_bucket++;
      if (curr_bucket>=MALLOC_TRACKING_HASH_ENTRIES) {
        dbg_current_trk = NULL;
      }
      else {
        dbg_current_trk = &malloc_tracking_table[curr_bucket];
      }
    }
  }
    
  list_remove(&trk->list);
  free(trk);  
}    
/*
**_______________________________________________________
** malloc tracking hook
**
** @param size    The size of the tracked memory area
** @param caller  The scaller PC
*/
static void * malloc_tracking_hook (size_t size, const void *caller) {
  void              * result;

  /* Restore all old hooks */
  restore_legacy_hooks();  
  
  /* Call recursively */
  result = malloc (size);
  
  /* Create traking entry */
  new_malloc_entry(result,size,caller);  
  if (malloc_tracking_trace) {
    info("malloc %p size %u from %p (%llu)",
        result,
	(unsigned int)size,
	caller,
	(long long unsigned int)malloc_tracking_entries);
  } 
  /* Re-install my hooks */
  install_my_hooks();
  
  return result;
}
/*
**_______________________________________________________
** memalign tracking hook
**
** @param alignment alignment size
** @param size      The size of the tracked memory area
** @param caller    The scaller PC
*/
static void * memalign_tracking_hook (size_t alignment, size_t size, const void *caller) {
  void              * result;

  /* Restore all old hooks */
  restore_legacy_hooks();  
  
  /* Call recursively */
  result = memalign (alignment,size);
  
  /* Create traking entry */
  new_malloc_entry(result,size,caller);  
  if (malloc_tracking_trace) {
    info("memalign %p alignment %d size %u from %p (%llu)",
        result,
	(unsigned int)alignment,
	(unsigned int)size,
	caller,
	(long long unsigned int)malloc_tracking_entries);
  } 
  /* Re-install my hooks */
  install_my_hooks();
  
  return result;
}
/*
**_______________________________________________________
** realloc tracking hook
**
** @param ptr       The address of the tracked memory area
** @param size      The size of the tracked memory area
** @param caller    The scaller PC
*/
static void * realloc_tracking_hook (void * ptr, size_t size, const void *caller) {
  malloc_tracking_t * trk;
  void              * result;
  
  /* Restore all old hooks */
  restore_legacy_hooks();  
  
  /* Call recursively */
  result = realloc (ptr,size);

  /* Replace tracking entry */
  trk = find_malloc_entry(ptr);  
  if (trk!=NULL) {     
    release_malloc_entry(trk);
  }  
  new_malloc_entry(result,size,caller);  
  
  if (malloc_tracking_trace) {
    info("realloc %p -> %p size %u from %p (%llu)",
         ptr, 
         result,
         (unsigned int)size,
	 caller,	 
         (long long unsigned int)malloc_tracking_entries);
  }    
  /* Re-install my hooks */
  install_my_hooks();  
 
  return result; 
}
/*
**_______________________________________________________
** free tracking hook
**
** @param ptr       The address of the tracked memory area
** @param caller    The scaller PC
*/
static void free_tracking_hook (void *ptr, const void *caller) {  
  malloc_tracking_t * trk;

  if (ptr == NULL) return;

  /* Restore all old hooks */
  restore_legacy_hooks();

  /* Call recursively */
  free (ptr);
  
  /* Relese tracking entry */
  trk = find_malloc_entry(ptr);
  if (trk!=NULL) {     
    release_malloc_entry(trk);
    if (malloc_tracking_trace) {
      info("free %p from %p (%llu)",
           ptr, 
  	   caller,		   
	   (long long unsigned int)malloc_tracking_entries);
    }	   
  } 
  else if (malloc_tracking_trace) {
    info("!!! free %p from %p (%llu)",
        ptr, 
  	caller,		
	(long long unsigned int)malloc_tracking_entries);  
  }
    
  /* Re-install my hooks */
  install_my_hooks();
}
/*
**_______________________________________________________
** enable malloc tracking
*/
static void malloc_tracking_enable (void) {
  int i;
    
  if (malloc_tracking_status!=0) return;
  malloc_tracking_status = 1;

  malloc_tracking_trace   = 0;
  dbg_current_trk         = NULL;
  malloc_tracking_entries = 0;
        
  for (i=0; i<MALLOC_TRACKING_HASH_ENTRIES; i++) {
    list_init(&malloc_tracking_table[i].list);
    malloc_tracking_table[i].size   = 0;
    malloc_tracking_table[i].hash   = i;
    malloc_tracking_table[i].addr   = 0;
    malloc_tracking_table[i].caller = 0;    
    malloc_tracking_table[i].ts     = 0;
  }  
  
  save_legacy_hooks();
  install_my_hooks();

  info("malloc tracking enabled");
}
/*
**_______________________________________________________
** disable malloc tracking
*/
static void malloc_tracking_disable (void) {
  malloc_tracking_t * trk;
  int i;
  
  if (malloc_tracking_status==0) return;
  malloc_tracking_status = 0;
  malloc_tracking_trace  = 0;
  dbg_current_trk        = NULL;

  restore_legacy_hooks();  
  
  for (i=0; i<MALLOC_TRACKING_HASH_ENTRIES; i++) {
  
    trk = list_1rst_entry(&malloc_tracking_table[i].list,malloc_tracking_t, list);
    while (trk != NULL) {
      release_malloc_entry(trk);
      trk = list_1rst_entry(&malloc_tracking_table[i].list,malloc_tracking_t, list);      
    }  
  }
  info("malloc tracking disabled");      
}
/*
**_______________________________________________________
** malloc tracking rozodiag function
*/
#define MAX_LISTING_ENTRY_COUNT 600
static inline char * malloc_tracking_dbg_list(char * pChar) {
  list_t * p=NULL;
  uint64_t now = time(NULL);
  int      entry_count = 0;
  int      first;
  int      curr_bucket;
  malloc_tracking_t * head;

  if (malloc_tracking_status==0) {
    pChar += sprintf(pChar,"malloc tracking is disabled\n");  
    return pChar;
  }
  
  /* End of list */
  if (dbg_current_trk == NULL) goto endoflist;
  
  first = curr_bucket = dbg_current_trk->hash;
  head  = &malloc_tracking_table[first];
  
  /* This is a head of list */
  if (dbg_current_trk == head) {
    p = dbg_current_trk->list.next;
  }
  /* This is a list element */
  else {
    p = &dbg_current_trk->list;
  }

  /* List some buckets */
  pChar += rozofs_string_append(pChar," _________________________ __________________ ________\n");
  pChar += rozofs_string_append(pChar,"|      address/size       |    caller PC     | age(s) |\n");
  pChar += rozofs_string_append(pChar,"|_________________________|__________________|________|\n");
  
  while(curr_bucket<MALLOC_TRACKING_HASH_ENTRIES) { 
    
    for ( ; p != &head->list; p = p->next) {

      dbg_current_trk = list_entry(p, malloc_tracking_t, list);
      if (entry_count > MAX_LISTING_ENTRY_COUNT) break;
    
      pChar += rozofs_string_append(pChar,"| ");
      pChar += rozofs_x64_append(pChar,(uint64_t)dbg_current_trk->addr);
      *pChar++ = '/';
      pChar += rozofs_u32_padded_append(pChar, 6, rozofs_left_alignment, dbg_current_trk->size);
      pChar += rozofs_string_append(pChar," | ");
      pChar += rozofs_x64_append(pChar,(uint64_t)dbg_current_trk->caller);
      pChar += rozofs_string_append(pChar," | ");	
      pChar += rozofs_u32_padded_append(pChar,6,rozofs_right_alignment,now-dbg_current_trk->ts);
      pChar += rozofs_string_append(pChar," |\n");
      
      entry_count++;
    }
    
    curr_bucket++;
    head    = &malloc_tracking_table[curr_bucket];
    dbg_current_trk = head;
    p = dbg_current_trk->list.next;
        
    if (entry_count > MAX_LISTING_ENTRY_COUNT) break;    

  }      
  
  pChar += rozofs_string_append(pChar,"|_________________________|__________________|________|\n");
  pChar += rozofs_string_append(pChar," ");
  pChar += rozofs_u32_append(pChar, entry_count); 
  *pChar++ = '/';  
  pChar += rozofs_u64_append(pChar, malloc_tracking_entries); 
  pChar += rozofs_string_append(pChar," entries from buckets ");
  pChar += rozofs_u32_append(pChar, first);
  *pChar++ = '-';
  pChar += rozofs_u32_append(pChar, curr_bucket);
  pChar += rozofs_eol(pChar);

  /* End of bucket list */
  if (curr_bucket >= MALLOC_TRACKING_HASH_ENTRIES) goto endoflist;
  
  pChar += rozofs_eol(pChar);
  return pChar;
  
endoflist:
  dbg_current_trk = NULL;
  pChar += rozofs_string_append(pChar," End of list\n");
  pChar += rozofs_eol(pChar);
  return pChar;
}
/*
**_______________________________________________________
** malloc tracking rozodiag function
*/

void malloc_tracking_dbg(char * argv[], uint32_t tcpRef, void *bufRef) {
  char *pChar = uma_dbg_get_buffer();
  malloc_tracking_t * trk;

  if (argv[1] == NULL) goto out;
  
  if (strcmp(argv[1],"enable")==0) {
    malloc_tracking_enable(); 
    goto out;
  }

  if (strcmp(argv[1],"disable")==0) {
    malloc_tracking_disable(); 
    goto out;
  }

  if (strcmp(argv[1],"trace")==0) {
    if (argv[2] == NULL) goto syntax;
    if (strcmp(argv[2],"off") == 0) {
      malloc_tracking_trace = 0; 	
      goto out;
    }

    if (strcmp(argv[2],"on") != 0) {
      goto syntax;
    }

    if (malloc_tracking_status != 0) { 
      malloc_tracking_trace = 1;  
    }	
    goto out;
  }        

  if (strcmp(argv[1],"first")==0) { 
    dbg_current_trk = &malloc_tracking_table[0];     
    pChar = malloc_tracking_dbg_list(pChar);
    return uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
  }
  
  if (strcmp(argv[1],"next")==0) { 
    pChar = malloc_tracking_dbg_list(pChar);
    return uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());  
  }  

  if (strcmp(argv[1],"content")==0) {
    int ret;
    void * addr;
    uint32_t size;

    if (argv[2]==NULL) {
      pChar += rozofs_string_append(pChar,"Missing address/size !!!\n");      
      goto syntax;
    }
    
    ret = sscanf(argv[2],"%p/%d", &addr, &size);
    if (ret != 2) {
      pChar += rozofs_string_append(pChar,"Bad address/size ");
      pChar += rozofs_string_append(pChar,argv[2]);
      pChar += rozofs_string_append(pChar," !!!\n");      
      goto syntax;
    }
    
    trk = find_malloc_entry(addr);
    if (trk == NULL) {
      pChar += rozofs_string_append(pChar,"No such allocated address ");
      pChar += rozofs_u64_append(pChar,(uint64_t)addr);
      pChar += rozofs_string_append(pChar," !!!\n");      
      goto syntax;
    }

    if (size > trk->size) size = trk->size;
    pChar = uma_dbg_hexdump(addr, size, pChar);
    return uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer()); 
  }
  
syntax:
  pChar += rozofs_string_append(pChar,"\nusage:\n");
  pChar += rozofs_string_append(pChar,"  malloc_tracking enable|disable\n");
  pChar += rozofs_string_append(pChar,"  malloc_tracking trace on|off\n");
  pChar += rozofs_string_append(pChar,"  malloc_tracking first|next\n");  
  pChar += rozofs_string_append(pChar,"  malloc_tracking content <addr>/<size>\n");  
  uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
  return;

out:
  pChar += sprintf(pChar,"\nmalloc tracking = %s\n",
                   malloc_tracking_status?"enabled":"disabled");
  pChar += sprintf(pChar,"malloc count    = %llu\n",
                   (long long unsigned int)malloc_tracking_entries);            
  pChar += sprintf(pChar,"malloc trace    = %s\n",
                   malloc_tracking_trace?"on":"off");
     
  uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
  return;
} 
/*
**_______________________________________________________
** register malloc tracking rozodiag function
*/
void malloc_tracking_register() {
  uma_dbg_addTopic("malloc_tracking", malloc_tracking_dbg);
}  
