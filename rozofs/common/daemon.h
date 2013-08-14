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

#ifndef _DAEMON_H
#define _DAEMON_H

/** API daemon management functions */

/** start a new daemon
 *
 * @param name: name used to identify this daemon (e.g. pid file)
 * @param on_start: pointer to function called at start up
 * @param on_stop: pointer to function called on SIGKILL or SIGTERM before exiting
 * @param on_hup: pointer to function called on SIGHUP
 */
void daemon_start(const char *name, void (*on_start) (void),
                  void (*on_stop) (void), void (*on_hup) (void));

#endif
