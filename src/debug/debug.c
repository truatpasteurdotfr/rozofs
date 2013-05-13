 
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
const char      *   prgName;  
/**
**   lnkdebug <IPADDR> <PORT>
*/
void syntax() {
  printf("\n%s [-i <IPaddr>] -p <port> [-c <cmd>] [-f <cmd file>] [-period <seconds>]\n\n",prgName);
  printf("-i <IPaddr>             destination IP address of the debug server\n");
  printf("                        default is 127.0.0.1\n");
  printf("-p <port>               destination port number of the debug server\n");
  printf("                        mandatory parameter\n"); 
  printf("-c <cmd>                command to run in one shot or periodically (-period)\n");                 
  printf("                        several -c options can be set\n");                 
  printf("-f <cmd file>           command file to run in one shot or periodically (-period)\n");         
  printf("                        several -f options can be set\n");                 
  printf("-period <seconds>       periodicity for running commands using -c or/and -f options\n");                 
  exit(0);
}
int debug_receive(int socketId) {
  int             ret;
  unsigned int    recvLen;
 
  printf("\n...............................................\n");

  while (1) {


    recvLen = 0;
    while (recvLen < sizeof(UMA_MSGHEADER_S)) {
      ret = recv(socketId,&msg,sizeof(UMA_MSGHEADER_S)-recvLen,0);
      if (ret <= 0) {
	    perror("error on recv1");
	    return 0;
      }
      recvLen += ret;
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
	    perror("error on recv2");
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
void read_file(const char * fileName ) {
  uint32_t len;  
  char * p;
  int fd;
    
  fd = open(fileName, O_RDONLY); 
  if (fd < 0) {
    printf("File %s not found\n",fileName);
    syntax();
  } 
  
  while (nbCmd < MAX_CMD) {

    len = readln (fd, msg.buffer,sizeof(msg.buffer));
    if (len == (uint32_t)-1) break;

    p = malloc(len+1);
    memcpy(p,msg.buffer,len);
    p[len] = 0;
    cmd[nbCmd++] = p;
    
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
    perror("send\n");
    printf("%d sent upon %d\n", sent,len);
    return -1;
  }
    
  if (!debug_receive(socketId)) {
    printf("Debug session abort\n");
    return -1;
  }  
  return 0;
  
}
void debug_interactive_loop(int socketId) {
//  char mycmd[1024]; 
  char *mycmd = NULL; 
//  int len;
//  int fd;
    
//  fd = open("/dev/stdin", O_RDONLY);
  using_history();
  rl_bind_key('\t',rl_complete);   
  while (1) {

    printf("\n_______________________________________________\n");
//    len = readln (fd, mycmd,sizeof(mycmd));
//    if (len == (uint32_t)-1) break;
    mycmd = readline ("rzdbg>");
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
    add_history(mycmd);    
    if (debug_run_this_cmd(socketId, mycmd) < 0)  break;
    free(mycmd);
  }
  if (mycmd != NULL) free(mycmd);
//  close(fd);  
} 
void debug_run_command_list(int socketId) {
  int idx;  

  for (idx=0; idx < nbCmd; idx++) {
    if (debug_run_this_cmd(socketId, cmd[idx]) < 0)  break;
  }
} 
void read_parameters(argc, argv)
int argc;
char *argv[];
{
  uint32_t            ret;
  uint32_t            idx;
  uint32_t            port32;

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
      ipAddr = inet_addr(argv[idx]);
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
    
    /* -c <command> */
    if (strcmp(argv[idx],"-c")==0) {
      char * p;
      int len;
      idx++;
      if (idx == argc) {
	printf ("%s option but missing value !!!\n",argv[idx-1]);
	syntax();
      }
      len = strlen(argv[idx]);
      p = malloc(len+1);
      memcpy(p,argv[idx],len);
      p[len] = 0;
      cmd[nbCmd] = p;
      nbCmd++;
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
  /*
  ** now create the socket for TCP
  */
  if ((socketId = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("Unable to create a socket !!!\n");
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
	perror ("unable to bind");
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
    perror("error on connect !!!");
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
  read_parameters(argc, argv);
  if (serverPort == 0) syntax();

  socketId = connect_to_server(ipAddr,serverPort);
  
  if (nbCmd == 0) {
    debug_interactive_loop(socketId);
  }
  else while(1) {
  
    debug_run_command_list(socketId);
    if (period == 0) break;
    sleep(period);
  }
  
  shutdown(socketId,2);
  close(socketId);
  exit(1);
}

