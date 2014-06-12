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

#ifndef GEOMGR_CONFIG_H
#define GEOMGR_CONFIG_H

#include <stdio.h>
#include <limits.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/list.h>

#define GEO_STRING_MAX 128

typedef struct _geomgr_config_calendar_entry_t {
  int                      start;
  int                      stop;
} geomgr_config_calendar_entry_t;



typedef struct _geomgr_config_calendar_t {
  int                                 nb_entries;
  geomgr_config_calendar_entry_t entries[1];
} geomgr_config_calendar_t;


typedef struct _geomgr_config_export_t {
  int                             active;
  char                            path[GEO_STRING_MAX];
  int                             site;
  int                             nb;
  geomgr_config_calendar_t * calendar;
  list_t                          list;
} geomgr_config_export_t;
 
 
typedef struct _geomgr_config_exportd_t {
    int                   active;
    char                  host[GEO_STRING_MAX];
    int                   nb_export;
    list_t                export; // list of export  
} geomgr_config_exportd_t;


typedef struct _geomgr_config_t {
    int                           active;
    int                           nb_exportd;    
    geomgr_config_exportd_t  exportd[1];
} geomgr_config_t;


void geomgr_config_release(geomgr_config_t *cfg);
geomgr_config_t * geomgr_config_read(const char *fname) ;

#endif
