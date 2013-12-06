#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <attr/xattr.h>

char * utility_name;
char * file;

void usage() {
    printf("Usage: %s <file name>\n",utility_name);
    exit(-1);
}


#define BUFFER_SIZE 1024
char value[BUFFER_SIZE];

#define ROZOFS_XATTR "rozofs"

int main(int argc, char **argv) {
  int size;
  struct stat stats;
  int idx;
  
  utility_name = argv[0];

    
  if (argc < 2) usage();

  for (idx=1; idx < argc; idx++) {

    file = argv[idx];
    
    if (lstat(file,&stats) < 0) {
      if (errno == ENOENT) printf("%20s : file does not exist\n", file);
      else                 printf("%20s : lstat(%s) %s\n", file, strerror(errno));
      continue;    
    }  

    size = getxattr(file,ROZOFS_XATTR,value,BUFFER_SIZE);
    if (size == -1) {
      printf("%20s : Not a RozoFS file\n",file);
      continue;
    }     
    value[size] = 0;
    printf("%20s : %s\n", file, value);
  }  
}
