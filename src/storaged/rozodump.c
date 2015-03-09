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
#include <uuid/uuid.h>

#include <rozofs/rozofs_srv.h>
#include "storage.h"

int firstBlock = 0;
char * filename[128] = {NULL};
int    fd[128] = {-1};

int    nb_file = 0;
int    block_number=-1;
int    bsize=0;
int    bbytes=-1;
int    dump_data=0;
unsigned int  first=0,last=-1;
unsigned int prjid = -1;

#define HEXDUMP_COLS 16
void hexdump(void *mem, unsigned int offset, unsigned int len) {
  unsigned int i, j;

  for(i = 0; i < len + ((len % HEXDUMP_COLS) ? (HEXDUMP_COLS - len % HEXDUMP_COLS) : 0); i++) {
          /* print offset */
    if(i % HEXDUMP_COLS == 0) {
      printf("0x%06x: ", i+offset);
    }

    /* print hex data */
    if(i < len) {
      printf("%02x ", 0xFF & ((char*)mem)[i]);
    }
    else /* end of block, just aligning for ASCII dump */{
      printf("%s","   ");
    }

    /* print ASCII dump */
    if(i % HEXDUMP_COLS == (HEXDUMP_COLS - 1)) {
      for(j = i - (HEXDUMP_COLS - 1); j <= i; j++) {
        if(j >= len) /* end of block, not really printing */{
          printf("%c",' ');
        }
        else if(isprint(((char*)mem)[j])) /* printable char */{
	  printf("%c",0xFF & ((char*)mem)[j]);        
        }
        else /* other char */{
          printf("%c",'.');
        }
      }	
      printf("%c",'\n');
    }
  }
}

int rozofs_storage_get_device_number(char * root) {
  int  device;
  char path[256];
  
  for (device=0; device < 122; device++) {
    sprintf(path, "%s/%d", root, device);
    if (access(path, F_OK) == -1) return device; 
  }
}

int read_hdr_file(char * root, int devices, int slice, rozofs_stor_bins_file_hdr_t * hdr, uuid_t uuid, int *spare) {
  char             path[256];
  int              dev;
  int              Zdev=-1;
  struct stat      st;
  int              nbHdr=0;
  uint64_t         ts=0;
  int              fd;
  int              safe;
  
  for (*spare=0; *spare<2; *spare+=1) {
  
    for (dev=0; dev < devices; dev++) {
    
      storage_build_hdr_path(path,root,dev, *spare, slice);
      storage_complete_path_with_fid(uuid,path);
      
      // Check that the file exists
      if (stat(path, &st) == -1) {
        continue;
      }

      // 1rst header file found
      if (Zdev == -1) {
        Zdev = dev;
	ts   = st.st_mtime;
        continue;
      }
      
      // An other header file found
      if (st.st_mtime > ts) {
        // More recently modified
        Zdev = dev;
	ts   = st.st_mtime;
        continue;	
      }	     
    }
    
    //Header file has been found. Read it 
    if (Zdev != -1) {
      storage_build_hdr_path(path,root,Zdev, *spare, slice);
      storage_complete_path_with_fid(uuid,path);
      // Open hdr file
      fd = open(path, ROZOFS_ST_NO_CREATE_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
      if (fd < 0) {
	printf("open(%s) %s",path, strerror(errno));
	return -1;
      }
      int nb_read = pread(fd, hdr, sizeof (*hdr), 0);
      if (nb_read < 0) {
        printf("pread(%s) %s",path, strerror(errno));
	return -1; 
      }
      close(fd);  
      printf("%15s : %s\n","Header file",path);
      printf("%15s : %d\n","version",hdr->version);
      printf("%15s : %d\n","layout",hdr->layout);
      printf("%15s : %d\n","bsize",hdr->bsize);
      printf("%15s : %d","distibution",hdr->dist_set_current[0]);
      safe    = rozofs_get_rozofs_safe(hdr->layout);
      int i;
      for (i=1; i< safe; i++) {
        printf("-%d",hdr->dist_set_current[i]);
      }
      printf("\n");
      printf("%15s : ","devices/chunk");
      i = 0;
      while(i<ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) {
        if (hdr->device[i] == ROZOFS_EOF_CHUNK) break;
	if (hdr->device[i] == ROZOFS_EMPTY_CHUNK) printf("/E");
	else                                      printf("/%d",hdr->device[i]);
	i++;
      }
      printf("\n");
      
      uint32_t save_crc32 = hdr->crc32;
      hdr->crc32 = 0;
      uint32_t crc32;
      
      if (save_crc32 != 0) {
        printf("%15s : 0x%x","crc32",save_crc32);
        crc32 = fid2crc32((uint32_t *)uuid);
        crc32 = crc32c(crc32,(char *) hdr, sizeof(rozofs_stor_bins_file_hdr_t));
        hdr->crc32 = save_crc32;
	
	if (save_crc32 != crc32) {
          printf(" expecting 0x%x !!!\n",crc32);
	}	
	else {
          printf(" OK\n");
	}   
      }	
      return 0; 
    }        
    
    // Check for spare
  }
  return -1;
}
char dateSting[128];
char * ts2string(uint64_t u64) {
  time_t   input = (u64/1000000);
  uint64_t micro = (u64%1000000);
  struct tm  ts;
  int len;
  
  if (u64==0) {
    sprintf(&dateSting[len],"Empty");
    return dateSting;    
  }

  ts = *localtime(&input);
  strftime(dateSting, sizeof(dateSting), "%a %Y-%m-%d %H:%M:%S:", &ts);
  len= strlen(dateSting);
  sprintf(&dateSting[len],"%6.6u",(unsigned int)micro);
  return dateSting;
}    
unsigned char buffer[2*1024*33];
void read_chunk_file(uuid_t fid, char * path, rozofs_stor_bins_file_hdr_t * hdr, int spare) {
  uint16_t rozofs_disk_psize;
  int      fd;
  rozofs_stor_bins_hdr_t * pH;
  int      nb_read;
  uint32_t bbytes = ROZOFS_BSIZE_BYTES(hdr->bsize);
  char     crc32_string[32];
  
  if (dump_data == 0) {
    printf ("+----------+------------+------------+----+------+-------+--------------------------------------------\n");
    printf ("| %8s | %10s | %10s | %2s | %4s | %5s | %s\n", "block","usr offset", "prj offset", "pj", "size", "crc32", "date");
    printf ("+----------+------------+------------+----+------+-------+--------------------------------------------\n");  
  }

  // Open bins file
  fd = open(path, ROZOFS_ST_NO_CREATE_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE_RO);
  if (fd < 0) {
    printf("open(%s) %s\n",path,strerror(errno));
    return;	
  }

  /*
  ** Retrieve the projection size on disk
  */
  if (spare) {
    rozofs_disk_psize = rozofs_get_max_psize_in_msg(hdr->layout,hdr->bsize);
  }  
  else if (prjid != -1) {
    rozofs_disk_psize = rozofs_get_psizes_on_disk(hdr->layout,hdr->bsize,prjid);
  }
  else {
    // Read 1rst block
    nb_read = pread(fd, buffer, sizeof(rozofs_stor_bins_hdr_t), 0);
    if (nb_read<0) {
      printf("pread(%s) %s\n",path,strerror(errno));
      return;      
    }
    pH = (rozofs_stor_bins_hdr_t*)buffer;
    if (pH->s.timestamp == 0) {
      printf("Can not tell projection id\n");
      return;            
    }
    rozofs_disk_psize = rozofs_get_psizes_on_disk(hdr->layout,hdr->bsize,pH->s.projection_id);
  }

  /*
  ** Reading a block
  */
  uint64_t offset = 0;
  int idx;
  nb_read = 1;
  uint64_t bid;
  
  
  while (nb_read) {
  
    // Read nb_proj * (projection + header)
    nb_read = pread(fd, buffer, rozofs_disk_psize*32, offset);
    if (nb_read<0) {
      printf("pread(%s) %s\n",path,strerror(errno));
      close(fd);
      return;         
    }
    
    nb_read = (nb_read / rozofs_disk_psize);
    
    pH = (rozofs_stor_bins_hdr_t*) buffer;
    for (idx=0; idx<nb_read; idx++) {
    
      pH = (rozofs_stor_bins_hdr_t*) &buffer[idx*rozofs_disk_psize];
      
      bid = (offset/rozofs_disk_psize)+idx;
      
      if ((bid < first) || (bid> last)) continue;
     
      uint32_t save_crc32 = pH->s.filler;
      pH->s.filler = 0;
      uint32_t crc32=0;

      if (save_crc32 == 0) {
        sprintf(crc32_string,"NONE");
      }
      else {
        crc32 = fid2crc32((uint32_t *)fid)+bid;
        crc32 = crc32c(crc32,(char *) pH, rozofs_disk_psize);
	if (crc32 != save_crc32) sprintf(crc32_string,"ERROR");
	else                     sprintf(crc32_string,"OK");
	
      }
      pH->s.filler = save_crc32;
      	
      if (dump_data == 0) {
      
	printf ("| %8llu | %10llu | %10llu | %2d | %4d | %5s | %s\n",
        	(long long unsigned int)bid,
        	(long long unsigned int)bbytes * bid,
        	(long long unsigned int)offset+(idx*rozofs_disk_psize),
		pH->s.projection_id,
		pH->s.effective_length, 
		crc32_string,  
		ts2string(pH->s.timestamp));
       }		
       else {
	printf("_________________________________________________________________________________________\n");
	printf("Block %llu / offset user %llu / offset projection %llu\n", 
        	(unsigned long long)bid, (unsigned long long)(bbytes * bid), (unsigned long long)(offset+(idx*rozofs_disk_psize)));
	printf("prj id %d / length %d / CRC %s / time stamp %s\n", 
        	pH->s.projection_id,pH->s.effective_length,crc32_string, ts2string(pH->s.timestamp)); 	
	printf("_________________________________________________________________________________________\n");
	if ((pH->s.projection_id == 0)&&(pH->s.timestamp==0)) continue;
	hexdump(pH, (offset+(idx*rozofs_disk_psize)), rozofs_disk_psize);      	            
      }
    }
    offset += (nb_read*rozofs_disk_psize);
  }
  if (dump_data == 0) {
    printf ("+----------+------------+------------+----+------+-------+--------------------------------------------\n");  
  }
  close(fd);
}    
char * utility_name=NULL;
char * input_file_name = NULL;
void usage() {

    printf("\nRozoFS projection file reader - %s\n", VERSION);
    printf("Usage: %s [OPTIONS] [PARAMETERS]\n",utility_name);
    printf("   PARAMETERS:\n");    
    printf("   -p <root path>       \tThe storage root path\n");
    printf("   -f <fid>             \tThe file FID\n");
    printf("   OPTIONS:\n");
    printf("   -h                   \tPrint this message.\n");
    printf("   -a                   \tGive the projection id in the case it can not be determine automatically.\n");    
    printf("   -d                   \tTo dump the data blocks and not only the block headers\n");
    printf("   -b <first>:<last>    \tTo display block numbers within <first> and <last>.\n"); 
    printf("   -b <first>:          \tTo display from block number <first> to the end.\n");
    printf("   -b :<last>           \tTo display from start to block number <last>.\n");  
    printf("   -b <block>           \tTo display only <block> block number.\n");   
    exit(-1);  
}


int main(int argc, char *argv[]) {
  int           idx=1;
  uuid_t        fid;
  char        * pFid=NULL;
  char        * pRoot=NULL;
  int           ret;
  int           slice;
  int           devices;
  rozofs_stor_bins_file_hdr_t hdr;
  char          path[256];
  int           spare;
   
  // Get utility name
  utility_name = basename(argv[0]); 
  
  if (argc < 3) usage(); 

  idx = 1;
  while (idx < argc) {

    /* -h */
    if (strcmp(argv[idx], "-h") == 0) usage();
    
    /* -d */
    if (strcmp(argv[idx], "-d") == 0) {
      idx++;
      dump_data = 1;
      continue;
    }
    
    /* -f */
    if (strcmp(argv[idx], "-f") == 0) {
      idx++;
      if (idx == argc) {
        printf("%s option set but missing value !!!\n", argv[idx-1]);
        usage();
      } 
      pFid = argv[idx];
      ret = uuid_parse( pFid, fid);
      if (ret != 0) {
        printf("%s is not a FID !!!\n", pFid);
        usage();
      }  
      idx++;
      continue;    
    }

    /* -p */
    if (strcmp(argv[idx], "-p") == 0) {
      idx++;
      if (idx == argc) {
        printf("%s option set but missing value !!!\n", argv[idx-1]);
        usage();
      } 
      pRoot = argv[idx];
      struct statfs st;
      if (statfs(pRoot, &st) != 0) {
        printf("%s is not a directory !!!\n", pRoot);
        usage();
      }
      idx++;
      continue;    
    }
    
    /* -a */
    if (strcmp(argv[idx], "-a") == 0) {
      idx++;
      if (idx == argc) {
        printf("%s option set but missing value !!!\n", argv[idx-1]);
        usage();
      } 
      ret = sscanf(argv[idx], "%u", &prjid);
      if (ret != 1) {
        printf("Bad projection identifier %s !!!\n",argv[idx]);
	usage();
      }
      idx++;
      continue;
    }
            
    /* -b */
    if (strcmp(argv[idx], "-b") == 0) {
      idx++;
      if (idx == argc) {
        printf("%s option set but missing value !!!\n", argv[idx-1]);
        usage();
      } 
      ret = sscanf(argv[idx], "%u:%u", &first, &last);
      if (ret == 2) {
        if (first>last) {
	  printf("first block index %d must be lower than last block index %d !!!\n", first, last);
	  usage();
	}
        idx++;
	continue;
      }
      
      ret = sscanf(argv[idx], ":%u", &last);
      if (ret == 1) {
        first = 0;
        idx++;
	continue;
      }
            
      ret = sscanf(argv[idx], "%u", &first);
      if (ret == 1) {
        if (argv[idx][strlen(argv[idx])-1]==':') {
          last = -1;
	}
	else {
	  last = first;
	}  
        idx++;
	continue;
      }
   
      printf("Bad -b option value \"%s\" !!!\n", argv[idx]);
      usage();
    }
    
    printf("No such option or parameter %s\n",argv[idx]);
    usage();
  }
  
  if ((pFid == NULL)||(pRoot == NULL)) {
    printf("Missing mandatory parameters !!!\n");
    usage();
  }

  rozofs_layout_initialize();
  
  /*
  ** Compute the FID slice
  */
  slice = rozofs_storage_fid_slice(fid);
  
  /*
  ** Get the number of devices
  */
  devices = rozofs_storage_get_device_number(pRoot);
  
  /*
  ** Find out a header file
  */
  if (read_hdr_file(pRoot,devices, slice, &hdr, fid, &spare)!= 0) {
    printf("No header file found for %s under %s !!!\n",pFid,pRoot);
    return;
  }
   
  int chunk = 0;   
  while(chunk<ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) {
  
      
    if (hdr.device[chunk] == ROZOFS_EOF_CHUNK) break;
  
    if (hdr.device[chunk] == ROZOFS_EMPTY_CHUNK) {
      printf ("\n============ CHUNK %d EMPTY ================\n", chunk);
      chunk++;
      continue;
    }
    
    storage_build_bins_path(path, pRoot, hdr.device[chunk], spare, slice);
    storage_complete_path_with_fid(fid, path);
    storage_complete_path_with_chunk(chunk,path);
    printf ("\n============ CHUNK %d ==  %s ================\n", chunk, path);

    read_chunk_file(fid,path,&hdr,spare);
    chunk++;
  }
  
}
