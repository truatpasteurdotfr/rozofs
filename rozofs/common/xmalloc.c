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
#include <stdint.h>
#include <sys/types.h>

#include "xmalloc.h"
#include "log.h"
#include <malloc.h>


typedef struct _xmalloc_stats_t
{
   uint64_t  count;
   int       size;
} xmalloc_stats_t;

#define XMALLOC_MAX_SIZE  512


xmalloc_stats_t *xmalloc_size_table_p = NULL;

void xmalloc_stats_insert(int n)
{
   int i;
   xmalloc_stats_t *p;
   
   for (i = 0; i < XMALLOC_MAX_SIZE; i++)
   {
      p= &xmalloc_size_table_p[i];
      if (p->size == n)
      {
         p->count++;
         return;      
      } 
      if (p->size == 0)
      {
         p->size = n;
         p->count = 1; 
         return;              
      }
   }   
}


void *xmalloc(size_t n) {
    void *p = 0;
    if (xmalloc_size_table_p == NULL)
    {
      xmalloc_size_table_p = memalign(32,sizeof(xmalloc_stats_t)*XMALLOC_MAX_SIZE);
      memset(xmalloc_size_table_p,0,sizeof(xmalloc_stats_t)*XMALLOC_MAX_SIZE);    
    }
    xmalloc_stats_insert((int)n);
    p = memalign(32,n);
    check_memory(p);
    return p;
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
