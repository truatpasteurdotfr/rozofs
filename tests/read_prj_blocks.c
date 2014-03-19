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
#include <pthread.h>
#include <sys/stat.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <unistd.h>
#include <libintl.h>
#include <sys/poll.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <libconfig.h>
#include <getopt.h>
#include <inttypes.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/vfs.h>
#include <ctype.h>

#include <rozofs/rozofs_srv.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/list.h>
#include <rozofs/rpc/mproto.h>
#include <rozofs/rpc/sproto.h>
#include <rozofs/rpc/spproto.h>
#include <rozofs/core/rozofs_core_files.h>
#include <rozofs/core/rozofs_ip_utilities.h>
#include <rozofs/rozofs_timer_conf.h>

#include "config.h"
#include "sconfig.h"
#include "storage.h"
#include "sconfig.h"
#include "rbs.h"
#include "rbs_eclient.h"

int layout = 0;
char * filename[128] = {NULL};
int    fd[128] = {-1};

int    nb_file = 0;
int    block_number=-1;


#define HEXDUMP_COLS 16
void hexdump(void *mem, unsigned int offset, unsigned int len)
{
        unsigned int i, j;
        
        for(i = 0; i < len + ((len % HEXDUMP_COLS) ? (HEXDUMP_COLS - len % HEXDUMP_COLS) : 0); i++)
        {
                /* print offset */
                if(i % HEXDUMP_COLS == 0)
                {
                        printf("0x%06x: ", i+offset);
                }
 
                /* print hex data */
                if(i < len)
                {
                        printf("%02x ", 0xFF & ((char*)mem)[i+offset]);
                }
                else /* end of block, just aligning for ASCII dump */
                {
                        printf("   ");
                }
                
                /* print ASCII dump */
                if(i % HEXDUMP_COLS == (HEXDUMP_COLS - 1))
                {
                        for(j = i - (HEXDUMP_COLS - 1); j <= i; j++)
                        {
                                if(j >= len) /* end of block, not really printing */
                                {
                                        putchar(' ');
                                }
                                else if(isprint(((char*)mem)[j+offset])) /* printable char */
                                {
                                        putchar(0xFF & ((char*)mem)[j+offset]);        
                                }
                                else /* other char */
                                {
                                        putchar('.');
                                }
                        }
                        putchar('\n');
                }
        }
}


int read_data_file() {
    int status = -1;
    uint64_t size = 0;
    int block_idx = 0;
    int idx =0;
    int count;
    rozofs_stor_bins_hdr_t * rozofs_bins_hdr_p;
    rozofs_stor_bins_footer_t * rozofs_bins_foot_p;
    char * loc_read_bins_p = NULL;

    uint16_t disk_block_size = (rozofs_get_max_psize(layout)*sizeof (bin_t)) + sizeof (rozofs_stor_bins_hdr_t) + sizeof (rozofs_stor_bins_footer_t);

    // Allocate memory for reading
    loc_read_bins_p = xmalloc(disk_block_size);   

    for (idx=0; idx < nb_file; idx++) {
      fd[idx] = open(filename[idx],O_RDWR);
      if (fd < 0) {
	  severe("Can not open file %s %s",filename[idx],strerror(errno));
	  goto out;
      }
    }
            
    printf (" ______ __________ ");
    for (idx=0; idx < nb_file; idx++) printf (" __________________ ______ ____ ");
    printf ("\n");

    printf("| %4s | %8s |","Blk","Offset");     
    for (idx=0; idx < nb_file; idx++) printf("| %16s | %4s | %2s |", "Time stamp", "lgth", "id");
    printf ("\n");  
    
    printf ("|______|__________|");
    for (idx=0; idx < nb_file; idx++) printf ("|__________________|______|____|");
    printf ("\n"); 
    
    if (block_number == -1) block_idx = 0;
    else                    block_idx = block_number;
    count = 1;
    
    while ( count ) {

      
      count = 0;
      printf("| %4d | %8d |",block_idx,block_idx*ROZOFS_BSIZE);

      for (idx=0; idx < nb_file; idx++) {
      
       if (fd[idx] == -1) {
         printf("%32s"," ");
	 continue;
       }	 

       size = pread(fd[idx],loc_read_bins_p,disk_block_size,block_idx*disk_block_size);
       
       if (size !=  disk_block_size) {
           printf ("|__________________|______|____|");
	   close(fd[idx]);
	   fd[idx] = -1;        
       }
       else {
           count++;
	   rozofs_bins_hdr_p = (rozofs_stor_bins_hdr_t *)loc_read_bins_p;
	   rozofs_bins_foot_p = (rozofs_stor_bins_footer_t *) ((bin_t*)(rozofs_bins_hdr_p+1)+rozofs_get_max_psize(layout));
	   
	   if (rozofs_bins_foot_p->timestamp != rozofs_bins_hdr_p->s.timestamp) {
	     printf("| xxxxxxxxxxxxxxxx | xxxx | xx |");	     
	   }
	   else if (rozofs_bins_hdr_p->s.timestamp == 0) {
	     printf("| %16d | %4d | .. |",0,0);
	   }
	   else {
	     printf("| %16llu | %4d | %2d |",
        	    (unsigned long long)rozofs_bins_hdr_p->s.timestamp,    
        	    rozofs_bins_hdr_p->s.effective_length,    
        	    rozofs_bins_hdr_p->s.projection_id);   
           }		  
       }

     }
     printf ("\n"); 
     
     block_idx++;
     if (block_number!=-1) break;
   }  	
   printf ("|______|__________|\n");

   if (block_number!=-1) {
      for (idx=0; idx < nb_file; idx++) {

       size = pread(fd[idx],loc_read_bins_p,disk_block_size,block_number*disk_block_size);
       
       if (size !=  disk_block_size) {
	   printf("Can not read block %d of %s\n", block_number, filename[idx]);       
       }
       else {
       	   rozofs_bins_hdr_p = (rozofs_stor_bins_hdr_t *)loc_read_bins_p;   
	   if (rozofs_bins_hdr_p->s.timestamp == 0) {
	     printf("Block %d of %s is a whole\n", block_number, filename[idx]);
	   }
	   else {
	     printf("Block %d of %s\n", block_number, filename[idx]);	     
	     hexdump(rozofs_bins_hdr_p+1, 0, disk_block_size-sizeof(rozofs_stor_bins_hdr_t));   
           }		  
       }

     }   
   }
     
   status = 0;
    			      
out:
    // This spare file used to exist but is not needed any more

    for (idx=0; idx < nb_file; idx++) {
      if (fd[idx] != -1) close(fd[idx]);
    }  
    if (loc_read_bins_p) {
      free(loc_read_bins_p);
      loc_read_bins_p = NULL;
    }	
    return status;
}


char * utility_name=NULL;
char * input_file_name = NULL;
void usage() {

    printf("RozoFS data file reader - %s\n", VERSION);
    printf("Usage: %s [OPTIONS]\n\n",utility_name);
    printf("   -h, --help\t\t\tprint this message.\n");
    printf("   -f, --file=<filename> \tA data file name to read.\n");
    printf("   -l, --layout=<layout> \tThe data file layout.\n"); 
    printf("   -b, --block=<block#>  \tThe block numlber to dump.\n");   
}

int main(int argc, char *argv[]) {
    int c;
    
    static struct option long_options[] = {
        { "help", no_argument, 0, 'h'},
        { "file", required_argument, 0, 'f'},	
        { "layout", required_argument, 0, 'l'},	
        { "block", required_argument, 0, 'b'},	
        { 0, 0, 0, 0}
    };

    // Get utility name
    utility_name = basename(argv[0]);   

    rozofs_layout_initialize();
   
    while (1) {

        int option_index = 0;
        c = getopt_long(argc, argv, "hH:f:l:b:", long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {

            case 'h':
                usage();
                exit(EXIT_SUCCESS);
                break;
                break;
            case 'f':
	        filename[nb_file++] = optarg;
                break;	
            case 'l':
	        {
		  int ret;
                  ret = sscanf(optarg,"%d", &layout);
                  if (ret <= 0) { 
                      fprintf(stderr, "dataReader failed. Bad layout: %s %s\n", optarg,
                              strerror(errno));
                      exit(EXIT_FAILURE);
                  }
		}
		break;
            case 'b':
	        {
		  int ret;
                  ret = sscanf(optarg,"%d", &block_number);
                  if (ret < 0) { 
                      fprintf(stderr, "dataReader failed. Bad block number: %s %s\n", optarg,
                              strerror(errno));
                      exit(EXIT_FAILURE);
                  }
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
    openlog("dataReader", LOG_PID, LOG_DAEMON);
    
    
    /*
    ** Check parameter consistency
    */
    if (nb_file == 0){
        fprintf(stderr, "dataReader failed. Missing --file option.\n");
        exit(EXIT_FAILURE);
    }  
 

    // Start rebuild storage   
    if (read_data_file() != 0) goto error;
    exit(EXIT_SUCCESS);
    
error:
    exit(EXIT_FAILURE);
}
