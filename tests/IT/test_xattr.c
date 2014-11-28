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
#include <attr/xattr.h>
#include <sys/wait.h>




#define DEFAULT_NB_PROCESS    20
#define DEFAULT_LOOP         200

int shmid;
#define SHARE_MEM_NB 7540

int nbProcess       = DEFAULT_NB_PROCESS;
int myProcId;
int loop=DEFAULT_LOOP;
int * result;
char mount[128];

static void usage() {
    printf("Parameters:\n");
    printf("-mount <mount point> ]  The mount point\n");
    printf("[ -process <nb> ]      The test will be done by <nb> process simultaneously (default %d)\n", DEFAULT_NB_PROCESS);
    printf("[ -loop <nb> ]        <nb> test operations will be done (default %d)\n",DEFAULT_LOOP);
    exit(-100);
}


#define BUFFER_SIZE 1024
char buff[BUFFER_SIZE];
char value[BUFFER_SIZE];
char expected_value[BUFFER_SIZE];

char * myAttributes[] = {
  "user.Attr1",
  "user.attr2",
  "user.cfhdqvhscvdsqvdssgfrqgtrthjytehCFREZGjlgnn3",
  "user.@4",
  "user.________________________________________________________5",
  "user.66666666666666666666666",
  "user.7.7.7.7.7",
  "user.DSQVFDQVFDSQFDVqjkmlngqmslq_cvkfdqbvsd8",

};
int nbAttr = (sizeof(myAttributes)/sizeof(char*));

static void read_parameters(argc, argv)
int argc;
char *argv[];
{
    unsigned int idx;
    int ret;
    
    mount[0] = 0;

    idx = 1;
    while (idx < argc) {
	
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
        /* -mount <mount point>  */
        if (strcmp(argv[idx], "-mount") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage();
            }
            ret = sscanf(argv[idx], "%s", mount);
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
			
        printf("Unexpected parameter %s\n", argv[idx]);
        usage();
    }
}
int get_attribute_rank(char * name) {
  int i;
  
  for (i=0; i < nbAttr; i++) {
    if (strcmp(name,myAttributes[i]) == 0) return i;
  }
  return -1;
}
int list_xattr (char * file,int option, int exist) {
  ssize_t size;
  char *pAttr=buff,*pEnd=buff;
  int nb;
  int idx;
  
  size = listxattr(file,buff,BUFFER_SIZE);
  if (size < 0)  {
    printf("listxattr(%s) %s\n", file, strerror(errno));
    return -1;
  } 
  
  if (!exist) {
    if (size != 0) {
      printf("Xattr does not exist but listxattr(%s) returns %d size\n", file,(int)size);
      return -1;
    }
    return 0;
  }
  
  if (size == 0) {
    printf("Xattr exist but listxattr(%s) returns 0 size\n", file);
    return -1;
  }


  pEnd += size;
  nb = 0;
  while (pAttr < pEnd) {
  
    idx = get_attribute_rank(pAttr);
    if (idx < 0) {
      printf("Unexpected attribute %s on file %s\n", pAttr, file);
      return -1;
    }  
    
    size = getxattr(file,pAttr,value,BUFFER_SIZE);
    if (size == -1) {
      printf("getxattr(%s) on file %s %s\n", pAttr, file, strerror(errno));
      return -1;
    }        

    value[size] = 0;
    nb++;
    if (option == XATTR_CREATE) {
      sprintf (expected_value, "%s.initial", myAttributes[idx]);
    }
    else {
      sprintf (expected_value, "%s.modified", myAttributes[idx]);
    }    
    if (strcmp(expected_value,value) != 0) {
      printf("read value %s while expecting %s for attr %s file %s\n", 
             value, expected_value, pAttr, file);
      return -1;
    }           
    pAttr += (strlen(pAttr)+1);
  }
  if (nb != nbAttr) {
      printf("Read %d attr while expecting %d\n", nb, nbAttr);
      return -1;
  }
  return 0;    
}
int set_attr (char * file, int option, int exist) {
  int idx,res;
  
  for (idx = 0 ; idx < nbAttr; idx++) {
  
    if (option == XATTR_CREATE) {
      sprintf (value, "%s.initial", myAttributes[idx]);
    }
    else {
      sprintf (value, "%s.modified", myAttributes[idx]);
    }

    res = setxattr(file, myAttributes[idx], value, strlen(value),option);
    
    if (option == XATTR_REPLACE) {
      if (res < 0) {
	printf("REPLACE setxattr(%s) on file %s %s\n", myAttributes[idx], file, strerror(errno));
	return -1;
      }   
    }
    else {
      if (exist) {
	if ((res >= 0)||(errno!=EEXIST)) {
	  printf("CREATE & exist setxattr(%s) on file %s %s\n", myAttributes[idx], file, strerror(errno));
	  return -1;
	}   
      }
      else {
	if (res < 0) {
	  printf("CREATE & !exist setxattr(%s) on file %s %s\n", myAttributes[idx], file, strerror(errno));
	  return -1;
	}   
      }
    }  
  }
  return 0;
}   
int remove_attr (char * file, int exist) {
  int idx,res;
  
  for (idx = 0 ; idx < nbAttr; idx++) {
    
    res = removexattr(file, myAttributes[idx]);
    if (exist) {
      if (res < 0) {
	  printf("exist .remove_attr(%s) on file %s %s\n", myAttributes[idx], file, strerror(errno));
	  return -1;
      }
    }
    else {
      if (res>= 0) {
        printf("!exist .remove_attr(%s) on file %s\n", myAttributes[idx], file);
        return -1; 
      }	
    }         
  }
  return 0;
}   
int do_one_test(char * file, int count) {
  int ret = 0;

  ret += list_xattr(file,XATTR_CREATE, 0);
  
  ret += set_attr (file,XATTR_CREATE, 0);
  ret += list_xattr(file,XATTR_CREATE, 1);
  
  ret += set_attr (file,XATTR_CREATE, 1);
  ret += list_xattr(file,XATTR_CREATE, 1);  

  ret += set_attr (file,XATTR_REPLACE, 1);
  ret += list_xattr(file,XATTR_REPLACE, 1);

  ret += remove_attr (file, 1);
  ret += list_xattr(file,XATTR_REPLACE, 0);

  ret += remove_attr (file, 0);
  ret += list_xattr(file,XATTR_REPLACE, 0);
  return ret;
}
int loop_test_process() {
  int count=0;   
  char filename[128];
  char cmd[128];
  pid_t pid = getpid();
       
  if (getcwd(cmd,125) == NULL) {
      return -1;
  }
  
  pid = getpid();
  sprintf(filename, "%s/%s/test_xattr.%u", cmd, mount, pid);
  
  sprintf(cmd, "echo HJKNKJNKNKhuezfqr > %s", filename);
  if (system(cmd) == -1) {
      return -1;
  }
  while (1) {
    count++;    
    if  (do_one_test(filename,count) != 0) {
      printf("proc %3d - ERROR in loop %d\n", myProcId, count); 
      unlink(filename);     
      return -1;
    } 
    if (loop==count) {
      unlink(filename);         
      return 0;
    }  
  }
}  
void free_result(void) {
  struct shmid_ds   ds;
  shmctl(shmid,IPC_RMID,&ds); 
}
int * allocate_result(int size) {
  struct shmid_ds   ds;
  void            * p;
      
  /*
  ** Remove the block when it already exists 
  */
  shmid = shmget(SHARE_MEM_NB,1,0666);
  if (shmid >= 0) {
    shmctl(shmid,IPC_RMID,&ds);
  }
  
  /* 
  * Allocate a block 
  */
  shmid = shmget(SHARE_MEM_NB, size, IPC_CREAT | 0666);
  if (shmid < 0) {
    perror("shmget(IPC_CREAT)");
    return 0;
  }  

  /*
  * Map it on memory
  */  
  p = shmat(shmid,0,0);
  if (p == 0) {
    shmctl(shmid,IPC_RMID,&ds);  
       
  }
  memset(p,0,size);  
  return (int *) p;
}
int main(int argc, char **argv) {
  pid_t pid[2000];
  int proc;
  int ret;
    
  read_parameters(argc, argv);
  
  if (mount[0] == 0) {
    printf("B-mount is mandatory\n");
    exit(-100);
  }

  if (nbProcess <= 0) {
    printf("Bad -process option %d\n",nbProcess);
    exit(-100);
  }

  result = allocate_result(4*nbProcess);
  if (result == NULL) {
    printf(" allocate_result error\n");
    exit(-100);
  }  
  for (proc=0; proc < nbProcess; proc++) {
  
     pid[proc] = fork();     
     if (pid[proc] == 0) {
       myProcId = proc;
       result[proc] = loop_test_process();
       exit(0);
     }  
  }

  for (proc=0; proc < nbProcess; proc++) {
    waitpid(pid[proc],NULL,0);        
  }
  
  ret = 0;
  for (proc=0; proc < nbProcess; proc++) {
    if (result[proc] != 0) {
      ret--;
    }
  }
  free_result();
  if (ret) printf("OK %d / FAILURE %d\n",nbProcess+ret, -ret);
  exit(ret);
}
