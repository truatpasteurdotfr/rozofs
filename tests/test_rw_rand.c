#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>

#define LINE_SZ 16
#if 0
print_char(char c) {
  if      ((c >= 'A') && (c <= 'Z')) ("\'%c\'",c);
  else if ((c >= 'a') && (c <= 'z')) printf("\'%c\'",c);
  else                               printf("x%2.2x",c);
}
#endif
print_char (char c)
{
  printf(" 0x%2.2x",c);
}

dump(char * p1, char * p2, int start, int stop) {
  int size= stop-start+1;
  int nb_lines=size/LINE_SZ;
  int line;
  int idx,i;
  
  printf("size = %d, nb_lines = %d\n",size,nb_lines);
  
  for (line=0; line < nb_lines; line++) {
    idx = start + line * LINE_SZ;
    printf("Offset %d (0x%x)\n",idx,idx);  
    for (i=0;i<LINE_SZ;i++) {
      print_char(p1[idx++]);
    }
    printf("\n");

    idx = start + line * LINE_SZ;
    for (i=0;i<LINE_SZ;i++) {
      print_char(p2[idx++]);
    }    
    printf("\n");

  }
  
  idx = start + line * LINE_SZ;
  printf("Offset %d\n",idx);  
  for (i=0;i<size%LINE_SZ;i++) {
    print_char(p1[idx++]);
  }
  printf("\n");  
  idx = start + line * LINE_SZ;
  for (i=0;i<size%LINE_SZ;i++) {
    print_char(p2[idx++]);
  }
  printf("\n");    
}

#define RANDOM_BUFFER_SIZE      (1024*128+1)
#define READ_BUFFER_SIZE         (2 * RANDOM_BUFFER_SIZE + 1)

#define DEFAULT_FILENAME "this_is_the_default_file_name"
#define DEFAULT_NB_PROCESS 4
char FILENAME[500];

#define ERRNO(s) 

char * pReadBuff    = NULL;
char * pCompareBuff = NULL;
char * pBlock       = NULL;
int nbProcess       = DEFAULT_NB_PROCESS;
int myProcId;
#define DEFAULT_LOOP 100
int loop=DEFAULT_LOOP;
int with_close = 1;

static void usage() {
    printf("Parameters:\n");
    printf("[ -file <name> ]   The create/write/read/delete test will be done on <name>.<procNumber> (default %s)\n", DEFAULT_FILENAME);
    printf("[ -process <nb> ]  The test will be done by <nb> process simultaneously (default %d)\n", DEFAULT_NB_PROCESS);
    printf("[ -loop <nb> ]     <nb> write/read operation will be done (default %d)\n",DEFAULT_LOOP);
    printf("[ -noclose ]       Do not close the file between write and read\n");
    exit(0);
}

static void read_parameters(argc, argv)
int argc;
char *argv[];
{
    unsigned int idx;
    int ret;

    strcpy(FILENAME, DEFAULT_FILENAME);

    idx = 1;
    while (idx < argc) {

        /* -file <name> */
        if (strcmp(argv[idx], "-file") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage();
            }
            ret = sscanf(argv[idx], "%s", FILENAME);
            if (ret != 1) {
                printf("%s option but bad value \"%s\"!!!\n", argv[idx-1], argv[idx]);
                usage();
            }
            idx++;
            continue;
        }
	
        /* -process <nb>  */
        if (strcmp(argv[idx], "-process") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage();
            }
            ret = sscanf(argv[idx], "%u", &nbProcess);
            if (ret != 1) {
                printf("%s option but bad value \"%s\"!!!\n", argv[idx-1], argv[idx]);
                usage();
            }
            idx++;
            continue;
        }
	
        /* -loop <nb>  */
        if (strcmp(argv[idx], "-loop") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage();
            }
            ret = sscanf(argv[idx], "%u", &loop);
            if (ret != 1) {
                printf("%s option but bad value \"%s\"!!!\n", argv[idx-1], argv[idx]);
                usage();
            }
            idx++;
            continue;
        }	
        /* -noclose   */
        if (strcmp(argv[idx], "-noclose") == 0) {
            idx++;
            with_close = 0;
            continue;
        }		
        printf("Unexpected parameter %s\n", argv[idx]);
        usage();
    }
}

int do_offset(char * filename, int offset, int blockSize) {
    int f;
    ssize_t size;
    int idx,idx2;
    int res = 1;


    f = open(filename, O_RDWR | O_CREAT, 0640);
    if (f == -1) {
        printf("proc %3d - open %d\n",myProcId, errno);
        printf("proc %3d - Can not open %s\n",myProcId, filename);
        return 0;
    }

    memcpy(&pCompareBuff[offset],pBlock,blockSize);
    size = pwrite(f, pBlock, blockSize, offset);
    if (size != blockSize) {
        printf("proc %3d - pwrite %d\n",myProcId,errno);
        printf("proc %3d - Can not write %s size %d at offset %d\n", myProcId, filename, blockSize, offset);
        close(f);
        return 0;
    }

    if (with_close) {
      f = close(f);
      if (f != 0) {
          printf("proc %3d - close %d\n",myProcId,errno);
          printf("proc %3d - Can not close %s\n", myProcId, filename);
          return 0;
      }

      f = open(filename, O_RDONLY);
      if (f == -1) {
          printf("proc %3d - re-open %d\n",myProcId,errno);
          printf("proc %3d - Can not re-open %s\n", myProcId,filename);
          return 0;
      }
    }
    
    memset(pReadBuff,0,READ_BUFFER_SIZE);
    size = pread(f, pReadBuff, READ_BUFFER_SIZE, 0);
    if (size <= 0) {
        printf("proc %3d - pread %d\n",myProcId,errno);
        printf("proc %3d - Can not read %s size %d\n", myProcId, filename, READ_BUFFER_SIZE);    
        close(f);
        return 0;
    }
    for (idx2 = 0; idx2 < READ_BUFFER_SIZE; idx2++) {
        if (pReadBuff[idx2] != pCompareBuff[idx2]) {
            printf("proc %3d - offset %d (0x%x) (read_req : %d (0x%x)) contains %x instead of %x\n", myProcId, idx2,idx2, 
                    READ_BUFFER_SIZE,READ_BUFFER_SIZE,pReadBuff[idx2],pCompareBuff[idx2]);
            dump(pReadBuff,pCompareBuff,idx2-30,idx2+29);
            res = 0;
	    break;
        }
    }
    
    f = close(f);
    if (f != 0) {
        printf("proc %3d - close %d\n",myProcId,errno);
        printf("proc %3d - Can not close %s\n", myProcId, filename);
    }
    return res;
}

int loop_test_process() {
  unsigned int blockSize;
  unsigned int offset;
  int count=0;    
  char filename[500];
  unsigned char c;
  int idx;      
  
  pBlock = NULL;
  pBlock = malloc(RANDOM_BUFFER_SIZE + 1);
  if (pBlock == NULL) {
      printf("Can not allocate %d bytes\n", RANDOM_BUFFER_SIZE+1);
      perror("malloc");
      return 0;
  }  
  c = 'A';
  int mark=0;
  for (idx = 0; idx < RANDOM_BUFFER_SIZE; idx++) {
      pBlock[idx] = c;
      if      (c == 'Z'){
        idx++;
	pBlock[idx] = mark++;
        c = 'a';
      }	
      else if (c == 'z') {
        idx++;
	pBlock[idx] = mark++;
        c = 'A';
      }	
      else c++;
  }
  pBlock[RANDOM_BUFFER_SIZE] = 0;
    
  // Prepare a working buffer to read from or write to the file
  pReadBuff = NULL;
  pReadBuff = malloc(READ_BUFFER_SIZE);
  if (pReadBuff == NULL) {
      printf("Can not allocate %d bytes\n", READ_BUFFER_SIZE);
      perror("malloc");
      return 0;
  }
  memset(pReadBuff,0,READ_BUFFER_SIZE);

  // Buffer containing what the file should contain
  pCompareBuff = NULL;
  pCompareBuff = malloc(READ_BUFFER_SIZE);
  if (pCompareBuff == NULL) {
      printf("Can not allocate %d bytes\n", READ_BUFFER_SIZE);
      perror("malloc");
      return 0;
  }
  memset(pCompareBuff,0,READ_BUFFER_SIZE);  
    
  sprintf(filename,"%s.%d",FILENAME, myProcId);
  if (unlink(filename) == -1) {
    if (errno != ENOENT) {
      printf("proc %3d - ERROR !!! can not remove %s %s\n", filename, strerror(errno));
      return 0;
    }
  }
  int pid = getpid();
      
  while (1) {

    offset    = (random()+pid) % RANDOM_BUFFER_SIZE; 
    blockSize = (random()+pid) % RANDOM_BUFFER_SIZE;
    if (blockSize == 0) blockSize = 1;
    count++;
    
    if (do_offset(filename,offset,blockSize) == 0) {
      printf("proc %3d - ERROR !!! blocksize %6d  - offset %6d\n", myProcId, blockSize, offset);
      printf("proc %3d : %d loop %s close\n",myProcId, count,with_close?"with":"without");
      return 0;
    }  
    
    if (loop==count) return 1;
  }
}  
int main(int argc, char **argv) {
  pid_t pid[2000];
  int proc;
  int ret;
  char cmd[128];
    
  read_parameters(argc, argv);

  if (nbProcess <= 0) {
    printf("Bad -process option %d\n",nbProcess);
    exit(0);
  }


  sprintf(cmd,"echo "" > /tmp/result_rw_random"); 
  system(cmd);
     
  for (proc=0; proc < nbProcess; proc++) {
  
     pid[proc] = fork();     
     if (pid[proc] == 0) {
       myProcId = proc;
       ret = loop_test_process();
       sprintf(cmd,"echo \"Process %3d result %s\" >> /tmp/result_rw_random",proc,ret?"OK":"FAILURE"); 
       system(cmd);      
       exit(0);
     }  
  }

  ret = 0;
  for (proc=0; proc < nbProcess; proc++) {
    waitpid(pid[proc],NULL,0);        
  }
  system("cat /tmp/result_rw_random");

  exit(0);
}
