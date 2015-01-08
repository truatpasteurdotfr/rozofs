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
 
#ifndef ROZOFS_QUOTA_INTF_H
#define ROZOFS_QUOTA_INTF_H
#include <stdint.h>
#include <linux/quota.h>
#include <rozofs/core/rozofs_socket_family.h>
#include "export.h"
#include <sys/types.h>        
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/un.h>

#define QIF_BLIMITS	(1 << QIF_BLIMITS_B)
#define QIF_SPACE	(1 << QIF_SPACE_B)
#define QIF_ILIMITS	(1 << QIF_ILIMITS_B)
#define QIF_INODES	(1 << QIF_INODES_B)
#define QIF_BTIME	(1 << QIF_BTIME_B)
#define QIF_ITIME	(1 << QIF_ITIME_B)
#define QIF_LIMITS	(QIF_BLIMITS | QIF_ILIMITS)
#define QIF_USAGE	(QIF_SPACE | QIF_INODES)
#define QIF_TIMES	(QIF_BTIME | QIF_ITIME)
#define QIF_ALL		(QIF_LIMITS | QIF_USAGE | QIF_TIMES)
/**
*  RozoFS quota opcodes
*/
#define ROZOFS_QUOTA_GETQUOTA 1
#define ROZOFS_QUOTA_SETQUOTA 2
#define ROZOFS_QUOTA_GETGRACE 3
#define ROZOFS_QUOTA_SETGRACE 4
#define ROZOFS_QUOTA_SET 5
typedef struct rozofs_qt_header_t
{
    uint32_t xid; /**< transaction identifier */
    uint32_t opcode;   /**< opcode of the operation */
    uint32_t length;   /**< length of the message   */
    int      pid;      /**< pid of the requester  */
} rozofs_qt_header_t;
/**
*____________________________________
*  ROZOFS_QUOTA_SETQUOTA
*____________________________________
*/
/*
** structure associated with set quota
*/
struct sq_dqblk {
	int64_t rq_bhardlimit;
	int64_t rq_bsoftlimit;
	int64_t rq_curblocks;
	int64_t rq_fhardlimit;
	int64_t rq_fsoftlimit;
	int64_t rq_curfiles;
	int64_t rq_btimeleft;
	int64_t rq_ftimeleft;
};
typedef struct sq_dqblk sq_dqblk;

/**
*  set quota interface request
*/
typedef struct _rozofs_setquota_req_t {
        rozofs_qt_header_t  hdr;
	int sqa_qcmd;    /**< what to set in the quota   */
	uint16_t eid;    /**< reference of the exportd   */
	int sqa_id;      /**< quota id                   */
	int sqa_type;    /**< quota type:USER or GROUP   */
	sq_dqblk sqa_dqblk;  /**< quota parameters       */
} rozofs_setquota_req_t;

/**
* set quota response
*/
typedef struct _rozofs_setquota_rsp_t {
        rozofs_qt_header_t  hdr;
	int status;    /**< OK or NOK      */
	int errcode;   /**< errno          */
} rozofs_setquota_rsp_t;

/**
*____________________________________
*  ROZOFS_QUOTA_GETQUOTA
*____________________________________
*/

struct rquota {
	int rq_bsize;         /**< block size */
	int rq_active;
	int64_t rq_bhardlimit;
	int64_t rq_bsoftlimit;
	int64_t rq_curblocks;
	int64_t rq_fhardlimit;
	int64_t rq_fsoftlimit;
	int64_t rq_curfiles;
	int64_t rq_btimeleft;
	int64_t rq_ftimeleft;
};
typedef struct rquota rquota;


/**
*  Get quota request
*/
typedef struct _rozofs_getquota_req {
        rozofs_qt_header_t  hdr;
	uint16_t eid;     /**< export identifier         */
	int gqa_type;     /**< quota type: user or group */
	int gqa_id;       /**< quota identifier          */
} rozofs_getquota_req_t ;

/**
*  Get quota response
*/
typedef struct _rozofs_getquota_rsp {
        rozofs_qt_header_t  hdr;
	int status;    /**< OK or NOK      */
	int errcode;   /**< errno          */
	rquota quota_data;  /**< requested quota information when status is 0 */
} rozofs_getquota_rsp_t ;


/**
*____________________________________
*  ROZOFS_QUOTA_GETGRACE
*____________________________________
*/

/**
*  Get grace period request
*/
typedef struct _rozofs_getgrace_req {
        rozofs_qt_header_t  hdr;
	uint16_t eid;     /**< export identifier           */
	int gqa_type;     /**< quota type: user or group   */
	int gqa_id;       /**< quota identifier : unused   */
} rozofs_getgrace_req_t ;

/**
*  Get grace period response
*/
typedef struct _rozofs_getgrace_rsp {
        rozofs_qt_header_t  hdr;
	int status;    /**< OK or NOK      */
	int errcode;   /**< errno          */
	rquota quota_data;  /**< requested quota information when status is 0 */
} rozofs_getgrace_rsp_t ;

/**
*____________________________________
*  ROZOFS_QUOTA_SETGRACE
*____________________________________
*/
/**
*  set quota interface request
*/
typedef struct _rozofs_setgrace_req_t {
        rozofs_qt_header_t  hdr;
	int sqa_qcmd;    /**< what to set in the quota   */
	uint16_t eid;    /**< reference of the exportd   */
	int sqa_id;      /**< quota id : unused           */
	int sqa_type;    /**< quota type:USER or GROUP   */
	sq_dqblk sqa_dqblk;  /**< quota parameters       */
} rozofs_setgrace_req_t;

/**
* set quota response
*/
typedef struct _rozofs_setgrace_rsp_t {
        rozofs_qt_header_t  hdr;
	int status;    /**< OK or NOK      */
	int errcode;   /**< errno          */
} rozofs_setgrace_rsp_t;


/**
*____________________________________
*  ROZOFS_QUOTA_SET STATE
*____________________________________
*/
/**
*  set quota interface request
*/
#define ROZOFS_QUOTA_ON 1
#define ROZOFS_QUOTA_OFF 0
typedef struct _rozofs_setquota_state_req_t {
        rozofs_qt_header_t  hdr;
	int cmd;         /**<either ROZOFS_QUOTA_ON or ROZOFS_QUOTA_OFF   */
	uint16_t eid;    /**< reference of the exportd   */
	int sqa_type;    /**< quota type:USER or GROUP   */
} rozofs_setquota_state_req_t;

/**
* set quota response
*/
typedef struct _rozofs_setquota_state_rsp_t {
        rozofs_qt_header_t  hdr;
	int status;    /**< OK or NOK      */
	int errcode;   /**< errno          */
} rozofs_setquota_state_rsp_t;
/*
**__________________________________________________________________________
*/
/**
* Build the Quota service af_unix pathname of a slave exportd

  @param socketname : pointer to a sockaddr_un structure
  @param instance_id : reference of the exportd slave
  
  @retval none
*/
static inline void rozofs_qt_set_socket_name_with_exp_slave_id(struct sockaddr_un *socketname,int instance_id)
{
  socketname->sun_family = AF_UNIX;  
  sprintf(socketname->sun_path,"%s_%d",ROZOFS_SOCK_FAMILY_QUOTA_NORTH_SUNPATH,instance_id);
}
/*
**__________________________________________________________________________
*/
/**
* Build the Quota service af_unix pathname of a requester

  @param socketname : pointer to a sockaddr_un structure
  @param pid : reference of the exportd slave
  
  @retval none
*/
static inline void rozofs_qt_set_socket_name_with_pid_of_requester(struct sockaddr_un *socketname,int pid)
{
  socketname->sun_family = AF_UNIX;  
  sprintf(socketname->sun_path,"%s_req_%d",ROZOFS_SOCK_FAMILY_QUOTA_NORTH_SUNPATH,pid);
}


/*__________________________________________________________________________
*/
/**
*  Send a quota request to an exportd slave
*
* @param hdr_p     pointer to the header of the request
* @param eid:      reference of the exportd slave
* @param fd:      reference of source socket
*
* @retval 0 on success 
  @retval -1 in case of error (see errno for details)
*  
*/
extern int quota_transactionId;

static inline int rozofs_qt_thread_intf_send(rozofs_qt_header_t  *hdr_p,int eid,int fd) 
{
  int  ret;
  int  instance_id;
  struct sockaddr_un path;
 
  /* Fill the transaction_id */
  quota_transactionId++;
  if (quota_transactionId == 0) quota_transactionId=1;
  hdr_p->xid  = quota_transactionId;  
  hdr_p->pid  = (int)getpid();
  

  /*
  ** build the af_unix pathname
  */
  instance_id = exportd_get_instance_id_from_eid(eid);
  rozofs_qt_set_socket_name_with_exp_slave_id( &path,instance_id);
  
  /* Send the buffer to its destination */
  ret = sendto(fd,hdr_p, hdr_p->length,0,(struct sockaddr*)&path,sizeof(path));
  if (ret <= 0) {
     severe("rozofs_qt_thread_intf_send csendto(%s) %s",path.sun_path, strerror(errno));
     return -1;  
  }
  return 0;
}

#endif
