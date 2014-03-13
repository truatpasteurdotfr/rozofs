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
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <libintl.h>
#include <getopt.h>
#include <inttypes.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>

#include <rozofs/rozofs_srv.h>
#include <rozofs/common/log.h>
#include "storage.h"

char * storaged_hostname = NULL;

/** Send a reload signal to the storio
 *
 * @param nb: Number of entries.
 * @param v: table of storages configurations to rebuild.
 */
int send_reload_to_storio() {
  char pid_file[128];
  int fd;
  int ret;
  int pid;

  if (storaged_hostname != NULL) {
      sprintf(pid_file, "%s%s_%s.pid", DAEMON_PID_DIRECTORY, STORIO_PID_FILE, storaged_hostname);
  } else {
      sprintf(pid_file, "%s%s.pid", DAEMON_PID_DIRECTORY, STORIO_PID_FILE);
  }  
  
  fd = open(pid_file, ROZOFS_ST_NO_CREATE_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
  if (fd < 0) {
    severe("open(%s) %s",pid_file,strerror(errno));
    return -1;
  }
  
  ret = pread(fd, &pid_file, sizeof(pid_file), 0);
  close(fd);
  if (ret <= 0) {
    severe("pread(%s) %s",pid_file,strerror(errno));
    return -1;
  }
  
  ret = sscanf(pid_file,"%u",&pid);
  if (ret != 1) {
    severe("sscanf(%s) %d",pid_file,ret);
    return -1;
  }
  
  kill(pid,1);
  return 0;
}


void usage() {
    printf("Send reload signal to storio - RozoFS %s\n", VERSION);
    printf("Usage: storio_reload [OPTIONS]\n\n");
    printf("   -h, --help\t\t\tprint this message.\n");
    printf("   -H, --host=storaged-host\tspecify the hostname of the storio to reload\n"); 
    printf("                           \t(default: none).\n");   
}

int main(int argc, char *argv[]) {
    int c;
    
    static struct option long_options[] = {
        { "help", no_argument, 0, 'h'},
        { "host", required_argument, 0, 'H'},	
        { 0, 0, 0, 0}
    };

   storaged_hostname = NULL;

    while (1) {

        int option_index = 0;
        c = getopt_long(argc, argv, "hH:", long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {

            case 'h':
                usage();
                exit(EXIT_SUCCESS);
                break;
            case 'H':
                storaged_hostname = optarg;
                break;
            case '?':
                usage();
                exit(EXIT_SUCCESS);
                break;
            default:
                usage();
                exit(EXIT_FAILURE);
                break;
        }
    }
    if (send_reload_to_storio()==0) {
      exit(EXIT_SUCCESS);
    }
    exit(EXIT_FAILURE);
}
