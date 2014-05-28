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

#ifndef GEO_PROFILER_H
#define GEO_PROFILER_H

#include <stdlib.h>
#include <string.h>
#include <rozofs/rozofs.h>

#ifdef __cplusplus
extern "C" {
#endif
#define GEO_IDX_COUNT 0
#define GEO_IDX_TMO 1
#define GEO_IDX_ERR 2
#define GEO_IDX_TIME 3


struct geo_one_profiler_t {
  uint64_t geo_sync_req[4];
  uint64_t geo_sync_get_next_req[4];
  uint64_t geo_sync_delete_req[4];
  uint64_t geo_sync_close_req[4];
};

typedef struct geo_one_profiler_t geo_one_profiler_t;


extern geo_one_profiler_t * geo_profiler[];
extern uint32_t             geo_profiler_eid;


#define START_GEO_PROFILING(the_probe)\
    uint64_t tic=0, toc;\
    struct timeval tv;\
    if (geo_profiler_eid <= EXPGW_EXPORTD_MAX_IDX) {\
       geo_one_profiler_t * prof = geo_profiler[geo_profiler_eid];\
       if (prof != NULL) {\
          prof->the_probe[GEO_IDX_COUNT]++;\
          gettimeofday(&tv,(struct timezone *)0);\
          tic = MICROLONG(tv);\
       }\
    }

#define STOP_GEO_PROFILING(the_probe,status)\
    if (geo_profiler_eid <= EXPGW_EXPORTD_MAX_IDX) {\
       geo_one_profiler_t * prof = geo_profiler[geo_profiler_eid];\
       if (prof != NULL) {\
          gettimeofday(&tv,(struct timezone *)0);\
          toc = MICROLONG(tv);\
	  switch (status) \
	  { \
	    case 0: \
	         break;\
	    case EAGAIN:\
	    prof->the_probe[GEO_IDX_TMO]++;\
	    break;\
	    default:\
	    prof->the_probe[GEO_IDX_ERR]++;\
	    break;\
	  }\
          prof->the_probe[GEO_IDX_TIME] += (toc - tic);\
       }\
    }
/*
*________________________________________________
* Allocate memory for export profiler statistics
*
* @param eid The export identifier
*
* @etval 0 on success, -1 on failure
*/    
static inline int geo_profiler_allocate(int eid) {

  if (eid>EXPGW_EXPORTD_MAX_IDX) return -1;
  
  if (geo_profiler[eid] != NULL) {
    free(geo_profiler[eid]);
    geo_profiler[eid] = NULL;
  }
  
  geo_profiler[eid] = malloc(sizeof(geo_one_profiler_t));
  if (geo_profiler[eid] == NULL) {
    return -1;	    
  }
  memset(geo_profiler[eid],0,sizeof(geo_one_profiler_t));
  return 0;
}  
/*
*________________________________________________
* Reset statistics of an export id
*
* @param eid The export identifier
*/    
static inline void geo_profiler_reset_one(int eid) {

  if (eid>EXPGW_EXPORTD_MAX_IDX) return;
  
  if (geo_profiler[eid] != NULL) {
    memset(geo_profiler[eid],0,sizeof(geo_one_profiler_t));
  }
} 
/*  
*________________________________________________
* Reset statistics of all export ids

*/    
static inline void geo_profiler_reset_all() {
  int eid;
  for (eid=0; eid <= EXPGW_EXPORTD_MAX_IDX; eid++) {
    if (geo_profiler[eid] != NULL) {
      memset(geo_profiler[eid],0,sizeof(geo_one_profiler_t));
    }    
  }
}   
/*
*________________________________________________
* Free allocated memory for export profiler statistics
*
* @param eid The export identifier
*/    
static inline void geo_profiler_free(int eid) {

  if (eid>EXPGW_EXPORTD_MAX_IDX) return;
  
  if (geo_profiler[eid] != NULL) {
    free(geo_profiler[eid]);
    geo_profiler[eid] = NULL;
  }
} 

#ifdef __cplusplus
}
#endif
#endif
