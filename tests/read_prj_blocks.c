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
int firstBlock = 0;
char * filename[128] = {NULL};
int    fd[128] = {-1};

int    nb_file = 0;
int    block_number=-1;
int    bsize=0;
int    bbytes=-1;


#define HEXDUMP_COLS 16
void hexdump(int blk, int prj, char * msg,void *mem, unsigned int offset, unsigned int len) {
  FILE * fd;
  unsigned int i, j;
  char fname[128];
  
  sprintf(fname,"b%d_sid%d.txt", blk, prj);
  fd = fopen(fname,"w");
  printf("%s\n",fname);
  fprintf(fd,"%s\n",msg);

  for(i = 0; i < len + ((len % HEXDUMP_COLS) ? (HEXDUMP_COLS - len % HEXDUMP_COLS) : 0); i++) {
          /* print offset */
    if(i % HEXDUMP_COLS == 0) {
      fprintf(fd,"0x%06x: ", i+offset);
    }

    /* print hex data */
    if(i < len) {
      fprintf(fd,"%02x ", 0xFF & ((char*)mem)[i+offset]);
    }
    else /* end of block, just aligning for ASCII dump */{
      fprintf(fd,"%s","   ");
    }

    /* print ASCII dump */
    if(i % HEXDUMP_COLS == (HEXDUMP_COLS - 1)) {
      for(j = i - (HEXDUMP_COLS - 1); j <= i; j++) {
        if(j >= len) /* end of block, not really printing */{
          fprintf(fd,"%c",' ');
        }
        else if(isprint(((char*)mem)[j+offset])) /* printable char */{
	  fprintf(fd,"%c",0xFF & ((char*)mem)[j+offset]);        
        }
        else /* other char */{
          fprintf(fd,"%c",'.');
        }
      }	
      fprintf(fd,"%c",'\n');
    }
  }
  
  fclose(fd);
}


char LINE[124];
int read_data_file() {
    int status = -1;
    uint64_t size = 0;
    int block_idx = 0;
    int idx =0;
    int count;
    rozofs_stor_bins_hdr_t * rozofs_bins_hdr_p;
    rozofs_stor_bins_footer_t * rozofs_bins_foot_p;
    char * loc_read_bins_p = NULL;
    int      forward = rozofs_get_rozofs_forward(layout);
//    int      inverse = rozofs_get_rozofs_inverse(layout);
    uint16_t disk_block_size; 
    uint16_t max_block_size = (rozofs_get_max_psize(layout,bsize)*sizeof (bin_t)) 
                            + sizeof (rozofs_stor_bins_hdr_t) + sizeof (rozofs_stor_bins_footer_t);
    char * p;
    int empty,valid;
    int prj_id;
        

    // Allocate memory for reading
    loc_read_bins_p = xmalloc(max_block_size);   

    for (idx=0; idx < nb_file; idx++) {
      if (strcmp(filename[idx],"NULL") == 0) {
        fd[idx] = -1;
      }
      else {
	fd[idx] = open(filename[idx],O_RDWR);
	if (fd < 0) {
	    severe("Can not open file %s %s",filename[idx],strerror(errno));
	    goto out;
	}
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
    
    empty = 0;
    while ( count ) {

      valid = 0;
      count = 0;
      
      p = &LINE[0];
      p += sprintf(p,"| %4d | %8d |",block_idx+firstBlock,(block_idx+firstBlock)*bbytes);

      for (idx=0; idx < nb_file; idx++) {
      
       if (fd[idx] == -1) {
         p += sprintf(p,"%32s"," ");
	 continue;
       }
       
       if (idx >= forward)
          disk_block_size = rozofs_get_max_psize_in_msg(layout, bsize);
       else
          disk_block_size = rozofs_get_psizes_on_disk(layout,bsize,idx);          
       
       size = pread(fd[idx],loc_read_bins_p,disk_block_size,block_idx*disk_block_size);
       
       if (size !=  disk_block_size) {
           p += sprintf(p,"|__________________|______|____|");
	   close(fd[idx]);
	   fd[idx] = -1;        
       }
       else {
         count++;
	 rozofs_bins_hdr_p = (rozofs_stor_bins_hdr_t *)loc_read_bins_p;
	 prj_id = rozofs_bins_hdr_p->s.projection_id;
	 
	 if (prj_id >= forward) {
	   valid = 1;
	   p += sprintf(p,"| xxxxxxxxxxxxxxxx | xxxx | %2d |",prj_id);	     
	 }
	 else {
           disk_block_size = (rozofs_get_psizes(layout,bsize,prj_id)*sizeof (bin_t));
           disk_block_size += sizeof (rozofs_stor_bins_hdr_t);
	   
	   rozofs_bins_foot_p = (rozofs_stor_bins_footer_t *) 
	            ((char*) rozofs_bins_hdr_p + disk_block_size);
           if (rozofs_bins_hdr_p->s.timestamp == 0) {
	     p += sprintf(p,"| %16d | .... | %2d |",0,prj_id);
	   }		    
	   else if (rozofs_bins_foot_p->timestamp != rozofs_bins_hdr_p->s.timestamp) {
	     valid = 1;
	     p += sprintf(p,"--%16.16llu----------%2d--", 
	                  (long long unsigned int)rozofs_bins_hdr_p->s.timestamp, 
			  prj_id);	     
	   }
	   else if (rozofs_bins_hdr_p->s.timestamp == 0) {
	     p += sprintf(p,"| %16d | .... | %2d |",0,prj_id);
	   }
	   else {
	     valid = 1;
	     p += sprintf(p,"| %16llu | %4d | %2d |",
        	    (unsigned long long)rozofs_bins_hdr_p->s.timestamp,    
        	    rozofs_bins_hdr_p->s.effective_length,    
        	    rozofs_bins_hdr_p->s.projection_id);   
           }
	 }  		  
       }

     }
     
     if (valid) {
       if (empty) {
         printf("... %d blocks...\n",empty);
	 empty = 0;
       }
       printf("%s\n",LINE); 
     }
     else {
       empty++;
     }
     block_idx++;
     if (block_number!=-1) break;
   }  	
   printf ("|______|__________|\n");

   if (block_number!=-1) {
      for (idx=0; idx < nb_file; idx++) {

       if (idx < forward) {	 
         disk_block_size = (rozofs_get_psizes(layout,bsize,idx)*sizeof (bin_t)) + sizeof (rozofs_stor_bins_hdr_t) + sizeof (rozofs_stor_bins_footer_t);
       }	 
       else {
         disk_block_size = (rozofs_get_max_psize(layout,bsize)*sizeof (bin_t)) + sizeof (rozofs_stor_bins_hdr_t) + sizeof (rozofs_stor_bins_footer_t);
       }  
       size = pread(fd[idx],loc_read_bins_p,disk_block_size,block_number*disk_block_size);
       if (size !=  disk_block_size) {
	   printf("Can not read block %d of %s\n", block_number, filename[idx]);       
       }
       else {
           char msg[256];
       	   rozofs_bins_hdr_p = (rozofs_stor_bins_hdr_t *)loc_read_bins_p;   
	   if (rozofs_bins_hdr_p->s.timestamp == 0) {
	     sprintf(msg,"%s Block %d is a whole\n", filename[idx], block_number);
	     hexdump(block_number,idx,msg, NULL, 0, 0);
	   }
	   else {
	     sprintf(msg,"%s Block %d size %d\n",filename[idx],block_number,  disk_block_size);	     
	     hexdump(block_number,idx,msg,rozofs_bins_hdr_p, 0, disk_block_size);   
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
    if (loc_read_bins_p != NULL) {
      //free(loc_read_bins_p);
      loc_read_bins_p = NULL;
    }
    return status;
}


char * utility_name=NULL;
char * input_file_name = NULL;
void usage() {

    printf("RozoFS data file reader - %s\n", VERSION);
    printf("Usage: %s [OPTIONS]\n\n",utility_name);
    printf("   -h, --help\n      print this message.\n");
    printf("   -f, --files=<f1>...<finverse>...<fforward>...<fsafe>\n");
    printf("      The list of files on the distribution.\n"); 
    printf("      NULL to tell the file is not present\n");
    printf("   -l, --layout=<layout> \tThe data file layout.\n"); 
    printf("   -b, --bsize=<block size>\tThe data block size (default %d)\n",bsize); 
    printf("   -c, --chunk=<chunk index>\tThe chunk number\n");
    printf("   -n, --blockNumber=<block#>  \tThe block number to dump.\n");   
}

int main(int argc, char *argv[]) {
    int c;
    
    static struct option long_options[] = {
        { "help", no_argument, 0, 'h'},
        { "file", required_argument, 0, 'f'},	
        { "layout", required_argument, 0, 'l'},	
        { "bsize", required_argument, 0, 'b'},	
        { "chunk", required_argument, 0, 'c'},	
        { "blockNumber", required_argument, 0, 'n'},	
        { 0, 0, 0, 0}
    };

    // Get utility name
    utility_name = basename(argv[0]);   

    rozofs_layout_initialize();
   
    while (1) {

        int option_index = 0;
        c = getopt_long(argc, argv, "hH:f:l:b:c:n:", long_options, &option_index);

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
            case 'c':
	        {
		  int ret;
                  ret = sscanf(optarg,"%d", &firstBlock);
                  if (ret <= 0) { 
                      fprintf(stderr, "dataReader failed. Bad chunk number: %s %s\n", optarg,
                              strerror(errno));
                      exit(EXIT_FAILURE);
                  }
		}
		break;		
            case 'n':
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
            case 'b':
	        {
		  int ret;
                  ret = sscanf(optarg,"%d", &bsize);
                  if (ret < 0) { 
                      fprintf(stderr, "dataReader failed. Bad block size: %s %s\n", optarg,
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
 
    bbytes = ROZOFS_BSIZE_BYTES(bsize);
    if (bbytes < 0) {
        fprintf(stderr, "bad block size: %d\n", bsize);
        exit(EXIT_FAILURE);
    }
    firstBlock *= ROZOFS_STORAGE_NB_BLOCK_PER_CHUNK(bsize);

    // Start rebuild storage   
    if (read_data_file() != 0) goto error;
    exit(EXIT_SUCCESS);
    
error:
    exit(EXIT_FAILURE);
}
