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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "mdirent_vers1.h"

/**
 *    MESSAGE : message to print out
 *   root_count : number of dirent root entries
 *   loop_count  number of entries per root count
 */
#define PRINT_TIME(loop_count,MESSAGE) \
   gettimeofday(&tv_stop,NULL); \
   { \
      uint64_t stop_time; \
      stop_time = tv_stop.tv_sec; \
      stop_time = stop_time*1000000; \
      stop_time += tv_stop.tv_usec; \
      uint64_t start_time; \
      start_time = tv_start.tv_sec; \
      start_time = start_time*1000000; \
      start_time += tv_start.tv_usec; \
      uint64_t delay_us; \
      uint64_t delay_ns;\
      delay_us = (stop_time - start_time)/(loop_count); \
      delay_ns = (stop_time - start_time) -(delay_us*(loop_count)); \
      printf( #MESSAGE " %d\n",loop_count);\
      printf("Delay %llu.%llu us\n", (long long unsigned int)(stop_time - start_time)/(loop_count),\
          (long long unsigned int)(delay_ns*10)/(loop_count));   \
      printf("Delay %llu us ( %llu s)\n", (long long unsigned int)(stop_time - start_time),   \
            (long long unsigned int)(stop_time - start_time)/1000000);\
      printf("Start Delay %u s %u us\n", (unsigned int)tv_start.tv_sec,(unsigned int)tv_start.tv_usec);   \
      printf("stop Delay %u s %u us\n", (unsigned int)tv_stop.tv_sec,(unsigned int)tv_stop.tv_usec);   \
   }

int main(int argc, char **argv) {
    int status = -1;
    int32_t j = 0;
    fid_t pfid;
    char pfid_str[37];
    mdir_t mdir;
    int32_t nb_mdirentries = 500000;
    struct timeval tv_start;
    struct timeval tv_stop;

    // Parent directory fid
    memset(pfid, 0, sizeof (fid_t));
    uuid_unparse(pfid, pfid_str);

    // Mkdir parent directory
    fprintf(stdout, "Create parent directory: %s\n", pfid_str);

    if (mkdir(pfid_str, S_IRWXU) == -1) {
        fprintf(stderr, "mkdir failed for directory %s: %s\n", pfid_str, strerror(errno));
        goto out;
    }

    // Open parent directory
    fprintf(stdout, "Open parent directory\n");

    if ((mdir.fdp = open(pfid_str, O_RDONLY, S_IRWXU)) < 0) {
        fprintf(stderr, "open failed for directory %s: %s\n", pfid_str, strerror(errno));
        goto out;
    }

    gettimeofday(&tv_start, NULL);
    // Put nb_mdirentries mdirentries
    fprintf(stdout, "Put %u mdirentries\n", nb_mdirentries);
    for (j = 0; j < nb_mdirentries; j++) {

        fid_t fid;
        uuid_generate(fid);
        char fid_str[37];
        uuid_unparse(fid, fid_str);
        uint32_t type = 1;
        char name[ROZOFS_FILENAME_MAX];
        sprintf(name, "file_%u", j);

        if (put_mdirentry(&mdir, name, fid, type) != 0) {
            fprintf(stderr, "Can't put mdirentry with name %s: %s\n", name, strerror(errno));
            goto out;
        }
    }
    PRINT_TIME(nb_mdirentries, put_mdirentry);

    gettimeofday(&tv_start, NULL);
    // Get nb_mdirentries mdirentries
    fprintf(stdout, "Get %u mdirentries\n", nb_mdirentries);
    for (j = 0; j < nb_mdirentries; j++) {

        fid_t fid;
        uint32_t type;
        char name[ROZOFS_FILENAME_MAX];
        sprintf(name, "file_%u", j);

        if (get_mdirentry(&mdir, name, fid, &type) != 0) {
            fprintf(stderr, "Can't get mdirentry with name: %s (%s)\n", name, strerror(errno));
            goto out;
        }
    }
    PRINT_TIME(nb_mdirentries, get_mdirentry);


    // Readdir nb_mdirentries mdirentries
    child_t *children;
    uint64_t cookie = 0;
    uint8_t eof = 0;

    gettimeofday(&tv_start, NULL);

    while (eof == 0) {
        if (list_mdirentries(&mdir, &children, cookie, &eof) != 0) {
            fprintf(stderr, "Can't list mdirentries: (%s)\n", strerror(errno));
            goto out;
        }
        cookie = cookie + 100;
    }

    PRINT_TIME(nb_mdirentries, list_mdirentries);

    gettimeofday(&tv_start, NULL);
    // Delete nb_mdirentries mdirentries
    fprintf(stdout, "Delete %u mdirentries\n", nb_mdirentries);
    j = 0;
    while (j < nb_mdirentries) {


        fid_t fid;
        uint32_t type;
        char name[ROZOFS_FILENAME_MAX];
        sprintf(name, "file_%u", j);

        if (del_mdirentry(&mdir, name, fid, &type) != 0) {
            fprintf(stderr, "Can't delete mdirentry with name: %s (%s)\n", name, strerror(errno));
            goto out;
        }
        j++;
    }
    PRINT_TIME(nb_mdirentries, del_mdirentry);

#if 0
    // Delete impair mdirentries
    fprintf(stdout, "Delete impair mdirentries\n");
    j = 0;
    while (j < nb_mdirentries) {

        if ((j % 2) == 0) {
            j++;
            continue;
        }

        fid_t fid;
        uint32_t type;
        char name[ROZOFS_FILENAME_MAX];
        sprintf(name, "file_%u", j);

        if (del_mdirentry(&mdir, name, fid, &type) != 0) {
            fprintf(stderr, "Can't delete mdirentry with name: %s (%s)\n", name, strerror(errno));
            goto out;
        }

        j++;
    }

    // Reput impair mdirentries
    fprintf(stdout, "RePut impair mdirentries\n");
    j = 0;
    while (j < nb_mdirentries) {

        if ((j % 2) == 0) {
            j++;
            continue;
        }

        fid_t fid;
        uuid_generate(fid);
        char fid_str[37];
        uuid_unparse(fid, fid_str);
        uint32_t type = 1;
        char name[ROZOFS_FILENAME_MAX];
        sprintf(name, "file_%u", j);

        if (put_mdirentry(&mdir, name, fid, type) != 0) {
            fprintf(stderr, "Can't put mdirentry with name %s: %s\n", name, strerror(errno));
            goto out;
        }

        j++;
    }

    // Delete nb_mdirentries mdirentries
    //fprintf(stdout, "Delete mdirentries 60 to 40 \n", nb_mdirentries);
    j = 60;
    while (j >= 40) {
        fid_t fid;
        uint32_t type;
        char name[ROZOFS_FILENAME_MAX];
        sprintf(name, "file_%u", j);

        if (del_mdirentry(&mdir, name, fid, &type) != 0) {
            fprintf(stderr, "Can't delete mdirentry with name: %s (%s)\n", name, strerror(errno));
            goto out;
        }

        j--;
    }

    // Reput pair mdirentries
    fprintf(stdout, "RePut mdirentries 40 to 60\n");
    j = 40;
    while (j <= 60) {

        fid_t fid;
        uuid_generate(fid);
        char fid_str[37];
        uuid_unparse(fid, fid_str);
        uint32_t type = 1;
        char name[ROZOFS_FILENAME_MAX];
        sprintf(name, "file_%u", j);

        if (put_mdirentry(&mdir, name, fid, type) != 0) {
            fprintf(stderr, "Can't put mdirentry with name %s: %s\n", name, strerror(errno));
            goto out;
        }

        j++;
    }

    // Delete nb_mdirentries mdirentries
    fprintf(stdout, "Delete %u mdirentries\n", nb_mdirentries);
    j = 0;
    while (j < nb_mdirentries) {


        fid_t fid;
        uint32_t type;
        char name[ROZOFS_FILENAME_MAX];
        sprintf(name, "file_%u", j);

        if (del_mdirentry(&mdir, name, fid, &type) != 0) {
            fprintf(stderr, "Can't delete mdirentry with name: %s (%s)\n", name, strerror(errno));
            goto out;
        }
        j++;
    }

    // Get nb_mdirentries mdirentries
    fprintf(stdout, "Try to get %u mdirentries\n", nb_mdirentries);
    for (j = 0; j < nb_mdirentries; j++) {

        fid_t fid;
        uint32_t type;
        char name[ROZOFS_FILENAME_MAX];
        sprintf(name, "file_%u", j);

        if (get_mdirentry(&mdir, name, fid, &type) == 0) {
            char fid_str[37];
            uuid_unparse(fid, fid_str);
            fprintf(stderr, "Failed: Get with suscess mdirentry with key name: %s (fid: %s, type: %u)\n", name, fid_str, type);
            goto out;
        }
    }


    // Put nb_mdirentries mdirentries
    fprintf(stdout, "Put %u mdirentries\n", nb_mdirentries);
    for (j = 0; j < nb_mdirentries; j++) {

        fid_t fid;
        uuid_generate(fid);
        char fid_str[37];
        uuid_unparse(fid, fid_str);
        uint32_t type = 1;
        char name[ROZOFS_FILENAME_MAX];
        sprintf(name, "file_%u", j);

        if (put_mdirentry(&mdir, name, fid, type) == 0) {
            //fprintf(stdout, "Put mdirentry with name: %s, fid: %s and type: %u\n", names, fid_str, type);
        } else {
            fprintf(stderr, "Can't put mdirentry with name %s: %s\n", name, strerror(errno));
            goto out;
        }
    }

    // Delete nb_mdirentries mdirentries
    fprintf(stdout, "Delete %u mdirentries\n", nb_mdirentries);
    j = 0;
    while (j < nb_mdirentries) {


        fid_t fid;
        uint32_t type;
        char name[ROZOFS_FILENAME_MAX];
        sprintf(name, "file_%u", j);

        if (del_mdirentry(&mdir, name, fid, &type) != 0) {
            fprintf(stderr, "Can't delete mdirentry with name: %s (%s)\n", name, strerror(errno));
            goto out;
        }
        j++;
    }

    // Get nb_mdirentries mdirentries
    fprintf(stdout, "Try to get %u mdirentries\n", nb_mdirentries);
    for (j = 0; j < nb_mdirentries; j++) {

        fid_t fid;
        uint32_t type;
        char name[ROZOFS_FILENAME_MAX];
        sprintf(name, "file_%u", j);

        if (get_mdirentry(&mdir, name, fid, &type) == 0) {
            char fid_str[37];
            uuid_unparse(fid, fid_str);
            fprintf(stderr, "Failed: Get with suscess mdirentry with key name: %s (fid: %s, type: %u)\n", name, fid_str, type);
            goto out;
        }
    }
#endif
    // Close parent directory
    if (close(mdir.fdp) == -1) {
        fprintf(stderr, "close failed for directory %s: %s\n", pfid_str, strerror(errno));
        goto out;
    }

    // Rmdir parent directory
    if (rmdir(pfid_str) == -1) {
        fprintf(stderr, "rmdir failed for directory %s: %s\n", pfid_str, strerror(errno));
        goto out;
    }

    fprintf(stdout, "TESTS OK\n");
    status = 0;
out:
    return status;
}
