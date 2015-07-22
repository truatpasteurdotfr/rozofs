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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#include "xmalloc.h"
#include "log.h"
#include <malloc.h>
#include <rozofs/core/uma_dbg_api.h>


xmalloc_stats_t *xmalloc_size_table_p = NULL;
uint32_t         xmalloc_entries = 0;

/*__________________________________________________________________________
*/
void show_xmalloc(char * argv[], uint32_t tcpRef, void *bufRef) {
    char *pChar = uma_dbg_get_buffer();
    int i;
    xmalloc_stats_t *p = xmalloc_size_table_p;

    if (xmalloc_size_table_p == NULL) {
        pChar += sprintf(pChar, "xmalloc stats not available\n");
        uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
        return;

    }

    for (i = 0; i < xmalloc_entries; i++,p++) {
        pChar += sprintf(pChar, "size %8.8u count %10.10llu = %llu\n", p->size, (long long unsigned int) p->count,
	                 (long long unsigned int)(p->size * (long long unsigned int)p->count));
    }
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());

}
/*__________________________________________________________________________
*/
void *xmalloc(size_t n) {
    void *p = 0;
    if (xmalloc_size_table_p == NULL)
    {
      xmalloc_size_table_p = memalign(32,sizeof(xmalloc_stats_t)*XMALLOC_MAX_SIZE);
      memset(xmalloc_size_table_p,0,sizeof(xmalloc_stats_t)*XMALLOC_MAX_SIZE);   
      uma_dbg_addTopic("xmalloc", show_xmalloc); 
    }
    xmalloc_stats_insert((int)n);
    p = memalign(32,n);
    check_memory(p);
    return p;
}
/*__________________________________________________________________________
*/
void xfree(void * p, size_t n) {
    xmalloc_stats_release(n);
    free(p);
}

void *xcalloc(size_t n, size_t s) {
    void *p = 0;

    p = calloc(n, s);
    check_memory(p);
    return p;
}

void *xrealloc(void *p, size_t n) {
    p = realloc(p, n);
    check_memory(p);
    return p;
}

char *xstrdup(const char *str) {
    char *p;

    p = strdup(str);
    check_memory(p);
    return p;
}
