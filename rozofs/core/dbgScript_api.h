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

#ifndef __dbgScript_api__H
#define __dbgScript_api__H

/* - OBJECT -------------------------------------------------------------------
NAME		dbgScript
TITLE		Debug interface
TYPE		interface object
ENTITY		dbgScript

PURPOSE
	This module maintains the initial configuration of the UNC-PS.

---------------------------------------------------------------------------- */

/* - FILE ---------------------------------------------------------------------
NAME		dbgScript_api.h
TITLE		dbgScript interface

OBJECT		dbgScript

CONTENTS
		dbgScript_init

---------------------------------------------------------------------------- */


/* - FUNCTION -----------------------------------------------------------------
NAME		dbgScript_init
TITLE		Ressources reservation and service initialization

SYNOPSIS
	rtc = dbgScript_init();
	
PARAMETERS
	rtc				Return code. Equals 0 if KO, 1 otherwise.

---------------------------------------------------------------------------- */
int				dbgScript_init(char * cfgPath);

#endif
