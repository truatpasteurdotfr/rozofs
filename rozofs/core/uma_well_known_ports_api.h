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
#ifndef UMA_WELL_KNOWN_PORTS_API_H
#define UMA_WELL_KNOWN_PORTS_API_H


/*
 * Monitoring input port 
 */
#define UMA_MONITOR_PORT            41233
/*
 * Debug ports
 */
#define UMA_DBG_SERVER_PORT_IGPU       41234
#define UMA_DBG_SERVER_PORT_UNCPS      41235
#define UMA_DBG_SERVER_PORT_PILOT      41236
#define UMA_DBG_SERVER_PORT_BSSGP_OAM  41237
#define UMA_DBG_SERVER_PORT_UOBS       41238
#define ROZOFS_DBG_SERVER_PORT_STORAGE_BASE       42000
#define ROZOFS_DBG_SERVER_PORT_EXPORTD_BASE       43000
#define ROZOFS_DBG_SERVER_TEST_BASE       44000
#endif
