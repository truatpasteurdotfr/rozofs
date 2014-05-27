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
 
#include <getopt.h>
#include <sys/param.h>  
 #include <stdint.h>
#include <stdlib.h>
 #include <string.h>
 #include <stdio.h>
 #include <errno.h>
 #include "geo_replication.h"
 
 
 
 void print_stats(geo_rep_srv_ctx_t *ctx_p)
 {
    printf("insert : %llu\n",(unsigned long long int)ctx_p->stats.insert_count);
    printf("update : %llu\n",(unsigned long long int)ctx_p->stats.update_count);
    printf("coll   : %llu\n",(unsigned long long int)ctx_p->stats.coll_count);
    printf("flush  : %llu\n",(unsigned long long int)ctx_p->stats.flush_count);
 
 }

/*
 *_______________________________________________________________________
 */
static void usage() {
    printf("Usage: ./track [OPTIONS]\n\n");
    printf("\t-h, --help\tprint this message.\n");
    printf("\t-p,--path <export_root_path>\t\texportd root path \n");
    printf("\t-c,--count <value>\t\tnumber of file to create(default: 10000) \n");
};

geo_rep_srv_ctx_t *ctx_p;

int main(int argc, char *argv[]) {
    int c;
    int read_attr_flag = 0;
    char *root_path=NULL;
    int ret ;
    int k;
    int limit = 1024;
    uint64_t off_start=0;
    uint64_t off_end=0;
    fid_t fid;
        
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"path", required_argument, 0, 'p'},
        {"count", required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };
      while (1) {

        int option_index = 0;
        c = getopt_long(argc, argv, "hc:p:", long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {

            case 'h':
                usage();
                exit(EXIT_SUCCESS);
                break;
            case 'p':
                root_path = optarg;
                break;
             case 'c':
                errno = 0;
                limit = (int) strtol(optarg, (char **) NULL, 10);
                if (errno != 0) {
                    strerror(errno);
                    usage();
                    exit(EXIT_FAILURE);
                }
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
    if (root_path == NULL) 
    {
	 usage();
	 exit(EXIT_FAILURE);  
    }

   
    ctx_p = geo_rep_init(1,1,root_path);
    if (ctx_p == NULL)
    {
      printf("geo_rep_init error %s\n",strerror(errno));
      exit(-1);
    }
    for (k= 0; k < limit; k++)
    {
      uuid_generate(fid);
      off_start = 0;
      off_end = k;
      ret = geo_rep_insert_fid(ctx_p,fid,off_start,off_end);
      if (ret < 0)
      {
	printf("geo_rep_insert_fid error %s\n",strerror(errno));
	exit(-1);
      }      
    }
    print_stats(ctx_p);
    ret = geo_rep_disk_flush(ctx_p);
    if (ret < 0)
    {
      printf("geo_rep_insert_fid error %s\n",strerror(errno));
      exit(-1);
    }    
    print_stats(ctx_p);
    printf("Done !\n");
 }
