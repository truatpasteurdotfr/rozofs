#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <attr/xattr.h>
#include <errno.h>
#include <string.h>

unsigned int loop=0;
unsigned int displayStatus=0;
#define BUFFER_SIZE 1024
char buff[BUFFER_SIZE];
char value[BUFFER_SIZE];

typedef enum  {
  TEST_MODE_GET,
  TEST_MODE_SET,
  TEST_MODE_LIST,
  TEST_MODE_ALL,
} TEST_MODE_E;
int test_mode = TEST_MODE_ALL;


char * myAttributes[] = {
  "user.Attr1",
  "user.attr2",
  "user.attr3",
  "user.attr4",
  "user.attr5"
};
int nbAttr = (sizeof(myAttributes)/sizeof(char*));

void display(char *fmt, ... ) {
  va_list         vaList;
  
  if (displayStatus == 0) return;
    
  va_start(vaList,fmt);
  vprintf(fmt, vaList);
  va_end(vaList);
   
}
void displayErrno(char *fmt, ... ) {
  va_list         vaList;
  
  if (displayStatus == 0) return;
    
  va_start(vaList,fmt);
  vsprintf(buff,fmt, vaList);
  va_end(vaList);
  
  perror(buff);
  
}
void usage() {
  printf("tst_xatrr [-loop <count>] [-display] [-mode <list|get|set> ] <mount point>\n");
  exit(0);
}
char * read_parameters(argc, argv)
     int	argc;
     char	*argv[];
{
  unsigned int        idx;
  int                 ret;
  
  idx = 1;
  while (idx < argc) {

    /* -loop <count> */
    if (strcmp(argv[idx],"-loop")==0) {
      idx++;
      if (idx == argc) {
	printf ("%s option without value !!!\n",argv[idx-1]);
	usage();
      }
      ret = sscanf(argv[idx],"%u",&loop);
      if (ret != 1) {
	printf ("%s option with bad value %s !!!\n",argv[idx-1],argv[idx]);
	usage();
      }
      idx++;
      continue;
    }
    
    /* -mode <list|get|set> */
    if (strcmp(argv[idx],"-mode")==0) {
      idx++;
      if (idx == argc) {
	printf ("%s option without value !!!\n",argv[idx-1]);
	usage();
      }
      if (strcmp(argv[idx],"list") == 0) {
        test_mode = TEST_MODE_LIST;
      }
      else if (strcmp(argv[idx],"get") == 0) {
        test_mode = TEST_MODE_GET;
      }
      else if (strcmp(argv[idx],"set") == 0) {
        test_mode = TEST_MODE_SET;
      }      
      else {
	printf ("%s option with bad value %s !!!\n",argv[idx-1],argv[idx]);
	usage();
      }
      idx++;
      continue;
    }    
    
    /* -display */
    if (strcmp(argv[idx],"-display")==0) {
      idx++;
      displayStatus=1;
      continue;      
    }    
    
    if (idx == (argc-1)) {
      return argv[idx];
    }
    printf ("Unexpected parameter %s !!!\n",argv[idx]);
    usage();
  }
  usage();
  return NULL;
}
void display_attributes (char * file ) {
  ssize_t size;
  char *pAttr=buff,*pEnd=buff;
  
  size = listxattr(file,buff,BUFFER_SIZE);
  if (size == -1)  displayErrno("listxattr");
  else {
    pEnd += size;
    display("\n\nDisplay attributes\n");
  }

  while (pAttr < pEnd) {
  
    size = getxattr(file,pAttr,value,BUFFER_SIZE);
    if (size == -1) displayErrno("getxattr(%s)", pAttr);
    else {
      value[size] = 0;
      display("- %s = %s\n", pAttr, value);
    }
    
    pAttr += (strlen(pAttr)+1);
  }
  
  
}
void set_attr (char * file, int option) {
  int idx,res;
  
  for (idx = 0 ; idx < nbAttr; idx++) {
  
    if (option == XATTR_CREATE) {
      sprintf (value, "%s.initial", myAttributes[idx]);
    }
    else {
      sprintf (value, "%s.modified", myAttributes[idx]);
    }
    display("\nSet %s to %s\n",myAttributes[idx],value);

    res = setxattr(file, myAttributes[idx], value, strlen(value),option);
    if (res == -1) {
      displayErrno("setxattr(%s,%s)",myAttributes[idx],value);
    } 
   
    display_attributes (file);
  }
}   
void remove_attr (char * file) {
  int idx,res;
  
  for (idx = 0 ; idx < nbAttr; idx++) {
    
    display("\nRemove %s\n",myAttributes[idx]);
    res = removexattr(file, myAttributes[idx]);
    if (res == -1) {
      displayErrno("removexattr(%s)",myAttributes[idx]);
    } 
   
    display_attributes (file);
  }
}   
void do_default_loop(char * file) {

  set_attr (file,XATTR_CREATE);
  set_attr (file,XATTR_CREATE);
  set_attr (file,XATTR_REPLACE);
  
  remove_attr(file);
}   
void do_loop_getxattr(char * file) {
  ssize_t size;

  size = getxattr(file,myAttributes[1],value,BUFFER_SIZE);
  if (size == -1) displayErrno("getxattr(%s)", myAttributes[1]);
}
void do_loop_setxattr(char * file) {
  int idx,res;
  
  for (idx = 0 ; idx < nbAttr; idx++) {
  
    sprintf (value, "Value.%s", myAttributes[idx]);
    res = setxattr(file, myAttributes[idx], value, strlen(value),XATTR_REPLACE);
    if (res == -1) {
      if (errno == ENOATTR) { 
        res = setxattr(file, myAttributes[idx], value, strlen(value),XATTR_CREATE);        
      }
      else {
        displayErrno("setxattr(%s,%s)",myAttributes[idx],value);
      }
    }
  }   
}
void do_loop_listxattr(char * file) {
  ssize_t size;
  
  size = listxattr(file,buff,BUFFER_SIZE);
  if (size == -1)  displayErrno("listxattr");
}
int main(int argc, char **argv) {
  char * mountPoint;
  pid_t  pid;
  char cmd[256];
  char name[125];

  mountPoint = read_parameters(argc,argv);
  if (mountPoint == NULL) return 0;

  getcwd(cmd,125);
  
  pid = getpid();
  sprintf(name, "%s/%s/test_xattr.%u", cmd, mountPoint, pid);
  
  sprintf(cmd, "echo HJKNKJNKNKhuezfqr > %s", name);
  system(cmd);  

  while (1) {
    switch(test_mode) {
      case TEST_MODE_LIST   : do_loop_listxattr(name);  break;
      case TEST_MODE_GET    : do_loop_getxattr(name);   break;
      case TEST_MODE_SET    : do_loop_setxattr(name);   break;
      default:                do_default_loop(name);
    }
    if (loop !=0) {
      loop--;
      if (loop == 0) return 0;
    }
    
  }
}  
   
 
