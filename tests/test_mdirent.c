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

#include <../src/mdirent.h>

int main(int argc, char **argv) {
    int status = -1;
    int32_t j = 0;
    fid_t pfid;
    char pfid_str[37];
    mdir_t mdir;
    int32_t nb_mdirentries = 10000;

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

    // Close parent directory
    if (close(mdir.fdp) == -1) {
        fprintf(stderr, "close failed for directory %s: %s", pfid_str, strerror(errno));
        goto out;
    }

    // Rmdir parent directory
    if (rmdir(pfid_str) == -1) {
        fprintf(stderr, "rmdir failed for directory %s: %s", pfid_str, strerror(errno));
        goto out;
    }

    fprintf(stdout, "TESTS OK\n");
    status = 0;
out:
    return status;
}
