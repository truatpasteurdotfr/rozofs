/*
  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
  This file is part of Rozofs.

  Rozofs is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  Rozofs is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <limits.h>
#include "rozofs.h"
#include "list.h"

typedef struct storage_config {
	sid_t sid;
	char root[FILENAME_MAX];
	list_t list;
} storage_config_t;

typedef struct sconfig {
	int layout;
	list_t storages;
} sconfig_t;

int sconfig_initialize(sconfig_t *config);

void sconfig_release(sconfig_t *config);

int sconfig_read(sconfig_t *config, const char *fname);

int sconfig_validate(sconfig_t *config);
