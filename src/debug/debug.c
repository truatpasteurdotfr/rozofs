 
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>          
#include <netinet/in.h> 
#include <netdb.h>
#include <netinet/tcp.h>
#include <signal.h>              
#include <errno.h>  
#include <stdarg.h>   
#include <unistd.h> 
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>  
#include <rozofs/core/uma_dbg_msgHeader.h>
#include <readline/readline.h>
#include <readline/history.h>

#define FIRST_PORT  9000
#define LAST_PORT  10000

#define DEFAULT_TIMEOUT 4

#define         MX_BUF (2048*8)
typedef struct  msg_s {
   UMA_MSGHEADER_S header;
   char            buffer[MX_BUF];
} MSG_S;
MSG_S msg;

#define MAX_CMD 1024
int                 nbCmd=0;
const char      *   cmd[MAX_CMD];
uint32_t            ipAddr;
uint16_t            serverPort;
uint32_t            period;
int                 allCmd;
const char      *   prgName;  
int                 timeout=DEFAULT_TIMEOUT;
char prompt[64];
/**
**   lnkdebug <IPADDR> <PORT>
*/
void syntax() {
  printf("\n%s [-i <hostname>] -p <port> [-c <cmd>] [-f <cmd file>] [-period <seconds>] [-t <seconds>]\n\n",prgName);
  printf("-i <hostname>           destination IP address or hostname of the debug server\n");
  printf("                        default is 127.0.0.1\n");
  printf("-p <port>               destination port number of the debug server\n");
  printf("                        mandatory parameter\n"); 
  printf("-c <cmd|all>            command to run in one shot or periodically (-period)\n");                 
  printf("                        several -c options can be set\n");                 
  printf("-f <cmd file>           command file to run in one shot or periodically (-period)\n");         
  printf("                        several -f options can be set\n");                 
  printf("-period <seconds>       periodicity for running commands using -c or/and -f options\n");                 
  printf("-t <seconds>            timeout value to wait for a response (default %d seconds)\n",DEFAULT_TIMEOUT);                 
  exit(0);
}
int debug_receive(int socketId) {
  int             ret;
  unsigned int    recvLen;
 
//  printf("\n...............................................\n");
  
  /* 
  ** Do a select before reading to be sure that a response comes in time
  */
  {
    fd_set fd_read;
    struct timeval to;
    
    to.tv_sec  = timeout;
    to.tv_usec = 0;

    FD_ZERO(&fd_read);
    FD_SET(socketId,&fd_read);
    
    ret = select(socketId+1, &fd_read, NULL, NULL, &to);
    if (ret != 1) {
      printf("Timeout %d sec\n",timeout);
      return 0;
    }
  }
  

  while (1) {


    recvLen = 0;
    char * p = (char *)&msg;
    while (recvLen < sizeof(UMA_MSGHEADER_S)) {
      ret = recv(socketId,p,sizeof(UMA_MSGHEADER_S)-recvLen,0);
      if (ret <= 0) {
        if (errno != 0) printf("error on recv1 %s",strerror(errno));
	return 0;
      }
      recvLen += ret;
      p += ret;
    }
    
    msg.header.len = ntohl(msg.header.len);    
    if (msg.header.len >= MX_BUF) {
      printf("Receive too big %d\n", msg.header.len);
      return 0;
    }

    recvLen = 0;
    while (recvLen < msg.header.len) {
      ret = recv(socketId,&msg.buffer[recvLen],msg.header.len-recvLen,0);
      if (ret <= 0) {
	    printf("error on recv2 %s",strerror(errno));
	    return 0;
      }
      recvLen += ret;
    }
    printf("%s", msg.buffer);
    if (msg.header.end) return 1;
  }
}

uint32_t readln(int fd,char *pbuf,uint32_t buflen)
{
   int len,lenCur = 0;
   for(;;)
   {
       len = read (fd, &pbuf[lenCur],1);
       if (len == 0)
       {
         return -1;
       }
       if (pbuf[lenCur] == '\n')
       {
         break;
       }
       lenCur++;
       if (lenCur == buflen)
       {
         lenCur--;
         break;
       }       
   }
   pbuf[lenCur] = 0;
   return lenCur+1;
}

void add_cmd_in_list(char * new_cmd, int len) {
  char * p;
  
  p = malloc(len+1);
  memcpy(p,new_cmd,len);
  p[len] = 0;
  cmd[nbCmd++] = (const char *) p;
}

void read_file(const char * fileName ) {
  uint32_t len;  
  int fd;
    
  fd = open(fileName, O_RDONLY); 
  if (fd < 0) {
    printf("File %s not found\n",fileName);
    syntax();
  } 
  
  while (nbCmd < MAX_CMD) {

    len = readln (fd, msg.buffer,sizeof(msg.buffer));
    if (len == (uint32_t)-1) break;

    add_cmd_in_list(msg.buffer, len);
  }
  
  close(fd);
} 
int debug_run_this_cmd(int socketId, const char * cmd) {
  uint32_t len,sent; 
   
  len = strlen(cmd)+1; 

  memcpy(msg.buffer ,cmd,len);  
  msg.header.len = htonl(len);
  msg.header.end = 1;
  msg.header.lupsId = 0;
  msg.header.precedence = 0;
  len += sizeof(UMA_MSGHEADER_S);

  sent = send(socketId, &msg, len, 0);
  if (sent != len) {
    printf("send %s",strerror(errno));
    printf("%d sent upon %d\n", sent,len);
    return -1;
  }
    
  if (!debug_receive(socketId)) {
    printf("Debug session abort\n");
    return -1;
  }  
  return 0;
  
}
#define SYSTEM_HEADER "system : "
void uma_dbg_read_prompt(int socketId) {
  int i=strlen(SYSTEM_HEADER);
  char *c = prompt;
    
  // Read the prompt
  if (debug_run_this_cmd(socketId, "who") < 0)  return;
  
  if (strncmp(msg.buffer,SYSTEM_HEADER, strlen(SYSTEM_HEADER)) == 0) {

    while(msg.buffer[i] != '\n') {
      *c = msg.buffer[i];
      c++;
      i++;
    }
    *c++ = '>';
    *c++ = ' ';     
    *c = 0;
  }
  else {
    strcpy(prompt,"rzdbg> ");
  }
}
#define LIST_COMMAND_HEADER "List of available topics :"
void uma_dbg_read_all_cmd_list(int socketId) {
  char * p, * begin;
  int len;
    
  // Read the command list
  if (debug_run_this_cmd(socketId, "") < 0)  return;

  nbCmd = 0;
  p = msg.buffer;
    
  if (strncmp(p,LIST_COMMAND_HEADER, strlen(LIST_COMMAND_HEADER)) != 0) return;  
  while(*p != '\n') p++;    
  p++;
    
  while (p) {
  
    // Skip ' '
    while (*p == ' ') p++;  
    
    // Is it the end
    if (strncmp(p,"exit", 4) == 0) break;
    
    // Read command in list
    begin = p;
    len = 0;
    while(*p != '\n') {
      len++;  
      p++;
    }
    add_cmd_in_list(begin, len);
    p++;
  }
}
void debug_interactive_loop(int socketId) {
//  char mycmd[1024]; 
  char *mycmd = NULL; 
//  int len;
//  int fd;

  uma_dbg_read_prompt(socketId);
  
//  fd = open("/dev/stdin", O_RDONLY);
  using_history();
  rl_bind_key('\t',rl_complete);   
  while (1) {

    printf("_________________________________________________________\n");
//    len = readln (fd, mycmd,sizeof(mycmd));
//    if (len == (uint32_t)-1) break;
    mycmd = readline (prompt);
    if (mycmd == NULL) break;
    if (strcasecmp(mycmd,"exit") == 0) {
      printf("Debug session end\n");
      break;
    }
    if (strcasecmp(mycmd,"q") == 0) {
      printf("Debug session end\n");
      break;
    }
    if (strcasecmp(mycmd,"quit") == 0) {
      printf("Debug session end\n");
      break;
    }
    if ((mycmd[0] != 0) && (strcasecmp(mycmd,"!!") != 0)) {
       add_history(mycmd);
    }
    if (debug_run_this_cmd(socketId, mycmd) < 0)  break;
    free(mycmd);
  }
  if (mycmd != NULL) free(mycmd);
//  close(fd);  
} 
void debug_run_command_list(int socketId) {
  int idx;  

  for (idx=0; idx < nbCmd; idx++) {
//    printf("_________________________________________________________\n");
//    printf("> %s", cmd[idx]);  
    if (debug_run_this_cmd(socketId, cmd[idx]) < 0)  break;
  }
} 


int expgw_host2ip(char *host,uint32_t *ipaddr_p)
{
    struct hostent *hp;    
    /*
    ** get the IP address of the storage node
    */
    if ((hp = gethostbyname(host)) == 0) {
        printf("gethostbyname failed for host : %s, %s", host,
                strerror(errno));
        return -1;
    }
    bcopy((char *) hp->h_addr, (char *) ipaddr_p, hp->h_length);
//    *ipaddr_p = ntohl(*ipaddr_p);
    return 0;
    
}

void read_parameters(argc, argv)
int argc;
char *argv[];
{
  uint32_t            ret;
  uint32_t            idx;
  uint32_t            port32;
  int                 status;

  idx = 1;
  /* Scan parameters */
  while (idx < argc) {
    
    /* -i <ipAddr> */
    if (strcmp(argv[idx],"-i")==0) {
      idx++;
      if (idx == argc) {
	printf ("%s option but missing value !!!\n",argv[idx-1]);
	syntax();
      }
//      ipAddr = inet_addr(argv[idx]);
      status = expgw_host2ip(argv[idx],&ipAddr);
      if (status < 0) 
      {
        syntax();      
      }
      idx++;
      continue;
    }

    /* -p <portNumber> */
    if (strcmp(argv[idx],"-p")==0) {
      idx++;
      if (idx == argc) {
	printf ("%s option but missing value !!!\n",argv[idx-1]);
	syntax();
      }
      ret = sscanf(argv[idx],"%u",&port32);
      if (ret != 1) {
	printf ("%s option with unexpected value \"%s\" !!!\n",argv[idx-1],argv[idx]);
        syntax();
      }	
      if ((port32<0) || (port32>0xFFFF)) {
	printf ("%s option with unexpected value \"%s\" !!!\n",argv[idx-1],argv[idx]);
	syntax();
      }
      serverPort = (uint16_t) port32;
      idx++;
      continue;
    }
    
    /* -period <period> */
    if (strcmp(argv[idx],"-period")==0) {
      idx++;
      if (idx == argc) {
	printf ("%s option but missing value !!!\n",argv[idx-1]);
	syntax();
      }
      ret = sscanf(argv[idx],"%u",&period);
      if (ret != 1) {
	printf ("%s option with unexpected value \"%s\" !!!\n",argv[idx-1],argv[idx]);   
        syntax();
      }
      idx++;
      continue;
    }
    
    /* -t <seconds> */
    if (strcmp(argv[idx],"-t")==0) {
      idx++;
      if (idx == argc) {
	printf ("%s option but missing value !!!\n",argv[idx-1]);
	syntax();
      }
      ret = sscanf(argv[idx],"%u",&timeout);
      if (ret != 1) {
	printf ("%s option with unexpected value \"%s\" !!!\n",argv[idx-1],argv[idx]);   
        syntax();
      }
      idx++;
      continue;
    }  
      
    /* -c <command> */
    if (strcmp(argv[idx],"-c")==0) {
      int len;
      idx++;
      if (idx == argc) {
	printf ("%s option but missing value !!!\n",argv[idx-1]);
	syntax();
      }
      if (strcmp(argv[idx],"all") == 0) {
        allCmd = 1;
      }
      else {
        len = strlen(argv[idx]);
        add_cmd_in_list(argv[idx], len);
      }	
      idx++;
      continue;
    }
        
    /* -f <fileName> */
    if (strcmp(argv[idx],"-f")==0) {
      idx++;
      if (idx == argc) {
	printf ("%s option but missing value !!!\n",argv[idx-1]);
	syntax();
      }
      read_file(argv[idx]);
      idx++;
      continue;
    }

    printf("Unexpected option \"%s\" !!!\n", argv[idx]);
    syntax();
  }  
  
}
int connect_to_server(uint32_t   ipAddr, uint16_t  serverPort) {
  int                 socketId;  
  uint16_t            port;
  struct  sockaddr_in vSckAddr;
  int                 sockSndSize = 256;
  int                 sockRcvdSize = 2*MX_BUF;
  int                  one=1;
  /*
  ** now create the socket for TCP
  */
  if ((socketId = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("Unable to create a socket !!!\n");
    exit(0);
  }  
  /* 
  ** Set REUSE_ADDR
  */  
  if (setsockopt (socketId,SOL_SOCKET,SO_REUSEADDR,(char*)&one,sizeof(one)) == -1)  {
    printf("Error on setsockopt SO_RCVBUF %d !!!\n",sockRcvdSize);
    close(socketId);
    exit(0);
  }
  
  /* Find a free port */
  for (port=FIRST_PORT; port < LAST_PORT; port++) {
    memset(&vSckAddr, 0, sizeof(struct sockaddr_in));
    vSckAddr.sin_family = AF_INET;
    vSckAddr.sin_port   = htons(port);
    vSckAddr.sin_addr.s_addr = INADDR_ANY;
    if ((bind(socketId,
	      (struct sockaddr *)&vSckAddr,
	      sizeof(struct sockaddr_in))) != 0) {
      if (errno ==EADDRINUSE) port++; /* Try next port */
      else {
	printf ("BIND ERROR %8.8x\n", vSckAddr.sin_addr.s_addr);
	printf ("unable to bind %s",strerror(errno));
	close(socketId);
	exit(0);
      }
    }
    else break;
  }
  if (port >= LAST_PORT) {
    printf("No more free port number between %d and %d !!!\n", FIRST_PORT, LAST_PORT);
    exit(0);
  }

  /* 
  ** change sizeof the buffer of socket for sending
  */
  if (setsockopt (socketId,SOL_SOCKET,
		  SO_SNDBUF,(char*)&sockSndSize,sizeof(int)) == -1)  {
    printf("Error on setsockopt SO_SNDBUF %d\n",sockSndSize);
    close(socketId);
    exit(0);
  }
  /* 
  ** change sizeof the buffer of socket for receiving
  */  
  if (setsockopt (socketId,SOL_SOCKET,
                  SO_RCVBUF,(char*)&sockRcvdSize,sizeof(int)) == -1)  {
    printf("Error on setsockopt SO_RCVBUF %d !!!\n",sockRcvdSize);
    close(socketId);
    exit(0);
  }
  

  /* Connect to the GG */
  vSckAddr.sin_family = AF_INET;
  vSckAddr.sin_port   = htons(serverPort);
  memcpy(&vSckAddr.sin_addr.s_addr, &ipAddr, 4); 
  if (connect(socketId,(struct sockaddr *)&vSckAddr,sizeof(struct sockaddr_in)) == -1) {
    printf("error on connect %s!!!", strerror(errno));
    exit(0);
  }
  return socketId;
}
int main(int argc, const char **argv) {
  int                 socketId;  

  prgName = argv[0];

  /* Read parametres */
  serverPort    = 0;
  ipAddr        = inet_addr("127.0.0.1");
  period        = 0;
  nbCmd         = 0;
  allCmd        = 0;
  read_parameters(argc, argv);
  if (serverPort == 0) syntax();

  socketId = connect_to_server(ipAddr,serverPort);

  if (allCmd) uma_dbg_read_all_cmd_list(socketId);
  
  if (nbCmd == 0) {
    debug_interactive_loop(socketId);
  }
  else while(1) {
  
    debug_run_command_list(socketId);
    if (period == 0) break;
    sleep(period);
  }
  
  close(socketId);
  exit(1);
}

