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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <ctype.h>
#include <sys/wait.h>
#include <storage.h>
/*
** ____________________________________________________________________
**  C O M P I L A T I O N
** 
** This source requires include file storage.h under src/storaged 
** which requires some other includes
**  
** gcc -I../src/storaged/ -I.. -I./build read_storage.c -g -o read_storage
** ____________________________________________________________________
**
*/
char FILENAME[500];
int rozofs_safe;  
int rozofs_psizes_max;
static void usage() {
    printf("\n./read_storage <file1> [<file2> ... [<fileN>]]\n\n");
    printf("eg: \n./read_storage storage_1-*/*/*/*/0853b843-dc9c-4d89-815b-d12805121f66.bins\n");
    printf("  to display the content of each pieces of file\n");
    printf("  whose FID is 0853b843-dc9c-4d89-815b-d12805121f66\n");    
    exit(-100);
}
void rozofs_layout_initialize(int layout) {
  int angleP[64];
  int angleQ[64];
  int rozofs_psizes[64];
  int i;
  int rozofs_forward;
  int rozofs_inverse;
  
  switch (layout) {
      case 0:
          rozofs_safe = 4;
	  rozofs_forward = 3;
	  rozofs_inverse = 2;
          break;
      case 1:
          rozofs_safe = 8;
	  rozofs_inverse = 4;
	  rozofs_forward = 6;
          break;
      case 2:
          rozofs_safe = 16;
	  rozofs_inverse = 8;
	  rozofs_forward = 12;
          break;
      default:
          break;
  }

 
   for (i = 0; i < rozofs_forward; i++) {
      angleP[i] = i - rozofs_forward / 2;
      angleQ[i] = 1;
      rozofs_psizes[i] = abs(i - rozofs_forward / 2) * (rozofs_inverse - 1)
                    + (ROZOFS_BSIZE / sizeof (pxl_t) / rozofs_inverse - 1) + 1;
      if (rozofs_psizes[i] > rozofs_psizes_max) rozofs_psizes_max = rozofs_psizes[i];
   }

}

int do_read_header(int fd, int display) {   
  size_t nb_read=0;
  rozofs_stor_bins_file_hdr_t file_hdr;
  int i;
   
  nb_read = pread(fd, &file_hdr, sizeof (file_hdr), 0);
  if (nb_read != sizeof (file_hdr)) {
    printf("Can not read header of size %d %s\n",sizeof (file_hdr), strerror(errno));
    return -1;
  }

  rozofs_layout_initialize(file_hdr.layout);
  if (!display) return file_hdr.layout;

  printf("  Version %d Layout %d  distribution current ",file_hdr.version, file_hdr.layout);

  for (i=0; i <rozofs_safe; i++) {
    printf("%d ",file_hdr.dist_set_current[i]);
  }  
  printf("/ next ");
  for (i=0; i <rozofs_safe; i++) {
    printf("%d ",file_hdr.dist_set_next[i]);
  }  
  printf("\n");
  
  return file_hdr.layout;
}
uint64_t start_time=0xFFFFFFFFFFFFFFFF;	   
int do_search_start_time(char * filename) {
   off_t offset = 0;
   rozofs_stor_bins_hdr_t bins_hdr;
   size_t nb_read=0;
   int idx=0;
   int layout;
   int fd;
     
  fd = open(filename, O_RDONLY , 0640);
  if (fd == -1) {
      printf("proc %3d - open %s %s\n",filename, strerror(errno));
      return -1;
  }

  layout = do_read_header(fd,0);
  if (layout == -1) {
    close (fd);
    return -1;
  }  

  while (1) {    
   
    offset = ((rozofs_psizes_max * sizeof (bin_t)+ sizeof(bins_hdr)) * idx++) + ROZOFS_ST_BINS_FILE_HDR_SIZE;
    
    nb_read = pread(fd, &bins_hdr, sizeof (bins_hdr), offset);
    if (nb_read != sizeof (bins_hdr)) return;

    if ((bins_hdr.s.timestamp==0) && (bins_hdr.s.effective_length==0)) continue;
  
    if (start_time > bins_hdr.s.timestamp) start_time = bins_hdr.s.timestamp;
  }
}
int do_read_block(int fd, int layout, int bid) {
   off_t offset = 0;
   rozofs_stor_bins_hdr_t bins_hdr;
   size_t nb_read=0;
    
   
  offset = ((rozofs_psizes_max * sizeof (bin_t)+ sizeof(bins_hdr)) * bid) + ROZOFS_ST_BINS_FILE_HDR_SIZE;
   
    // Write nb_proj * (projection + header)
  //  printf("offset 0x%8x \n",offset );
  nb_read = pread(fd, &bins_hdr, sizeof (bins_hdr), offset);
  if (nb_read != sizeof (bins_hdr)) return -1;

  if ((bins_hdr.s.timestamp==0) && (bins_hdr.s.effective_length==0))  return 0;

  printf ("    0x%8.8x %4d | %20llu | %8d | %6d | %3d | %3d |\n", 
           offset,bid,
           bins_hdr.s.timestamp, (int) (bins_hdr.s.timestamp - start_time),
           bins_hdr.s.effective_length, 
           bins_hdr.s.projection_id, 
           bins_hdr.s.version);
  return 0;
}
int do_read(char * filename) {   
  int fd;
  int block;
  int layout;
  
  printf("%s\n",filename);

  fd = open(filename, O_RDONLY , 0640);
  if (fd == -1) {
      printf("proc %3d - open %s %s\n",filename, strerror(errno));
      return -1;
  }

  layout = do_read_header(fd,1);
  if (layout == -1) {
    close (fd);
    return -1;
  }

  
  block = 0;
  printf ("     offset & block |       Time Stamp     | relative | length | Pid |Vers.|\n"); 
  printf ("    ----------------+----------------------+----------+--------+-----+-----+\n");

  while (do_read_block(fd,layout,block) == 0) block++;

  close(fd);
  return 0;
}

int main(int argc, char **argv) {
  int idx=1;

  if (argc <= 1) {
    usage(); 
  } 
  if (argv[1][0] == '-') {
    usage();     
  }

  start_time=0xFFFFFFFFFFFFFFFF;	 

  idx=1;
  while (idx < argc) {
    do_search_start_time(argv[idx]);
    idx++;
  }   
  
  idx=1;  
  while (idx < argc) {
    do_read(argv[idx]);
    idx++;
  }  
  return 0;
}
