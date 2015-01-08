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

/*
**   I N C L U D E  F I L E S
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <rozofs/common/log.h>

#include "ruc_common.h"
#include "ruc_buffer.h"
#include "ruc_list.h"
#include "ruc_buffer_api.h"
#include "ruc_trace_api.h"


uint32_t ruc_buffer_trace = FALSE;

/*
**  activate/de-activate the trace for the buffer services
**
**   TRUE = active /FALSE: inactive
*/

void ruc_buf_set_trace(uint32_t flag)
{
  ruc_buffer_trace = flag;
}


/*
**__________________________________________________________
*/

void * ruc_buf_poolCreate_shared(uint32_t nbBuf, uint32_t bufsize, key_t key/*,ruc_pf_buf_t init_fct*/)
{
  ruc_buf_t  *poolRef;
  ruc_obj_desc_t  *pnext=(ruc_obj_desc_t*)NULL;
  char *pusrData;
  char *pBufCur;
  ruc_buf_t  *p;
  int shmid;

//   RUC_BUF_TRC("buf_poolCreate",nbBuf,bufsize,-1,-1);
   /*
   **   create the control part of the buffer pool
   */
   poolRef = (ruc_buf_t*)ruc_listCreate(nbBuf,sizeof(ruc_buf_t));
   if (poolRef==(ruc_buf_t*)NULL)
   {
     /*
     ** cannot create the buffer pool
     */
     RUC_WARNING(-1);
     // 64BITS return (uint32_t)NULL;
     return NULL;
   }
   poolRef->type = BUF_POOL_HEAD;
   /*
   **  create the usrData part
   */
   /*
   **  bufsize MUST long word aligned
   */
   if ((bufsize & 0x3) != 0)
   {
     bufsize = ((bufsize & (~0x3)) + 4 );
   }
  /*
  ** test that the size does not exceed 32 bits
  */
  {
    uint32_t nbElementOrig;
    uint32_t NbElements;
    uint32_t memRequested;

    nbElementOrig = nbBuf;
    if (nbElementOrig == 0)
    {
      RUC_WARNING(-1);
      // 64BITS return (uint32_t)NULL;
      return NULL;
    }

    memRequested = bufsize*(nbElementOrig);
    NbElements = memRequested/(bufsize);
    if (NbElements != nbElementOrig)
    {
      /*
      ** overlap
      */
      RUC_WARNING(-1);
      // 64BITS return (uint32_t)NULL;
      return NULL;
    }
  }
  /*
  ** create the shared memory
  */
  if ((shmid = shmget(key, bufsize*nbBuf, IPC_CREAT | 0666 )) < 0) {
      severe("ruc_buf_poolCreate_shared :shmget %s",strerror(errno));
 //     RUC_WARNING(errno);
      return (ruc_obj_desc_t*)NULL;
  }
  /*
  * Now we attach the segment to our data space.
  */
  if ((pusrData = shmat(shmid, NULL, 0)) == (char *) -1)
  {
     /*
     **  out of memory, free the pool
     */    perror("shmat");
    RUC_WARNING(errno);
    ruc_listDelete_shared((ruc_obj_desc_t*)poolRef);
    return (ruc_obj_desc_t*)NULL;
  }
   /*
   ** store the pointer address on the head
   */
   poolRef->ptr = (uint8_t*)pusrData;
   poolRef->bufCount = nbBuf;
   poolRef->len = (uint32_t)nbBuf*bufsize;
   poolRef->usrLen = nbBuf;

   pBufCur = pusrData;
   /*
   ** init of the payload pointers
   */
   while ((p = (ruc_buf_t*)ruc_objGetNext((ruc_obj_desc_t*)poolRef,&pnext))
               !=(ruc_buf_t*)NULL)
   {
      p->ptr = (uint8_t*)pBufCur;
      p->state = BUF_FREE;
      p->bufCount  = (uint16_t)bufsize;
      p->type = BUF_ELEM;
      p->callBackFct = (ruc_pf_buf_t)NULL;
#if 0
      /*
      ** call the init function associated with the buffer
      */
      if (init_fct != NULL) (*init_fct)(pBufCur);
#endif
      pBufCur += bufsize;
   }
   /*
   **  return the reference of the buffer pool
   */
  RUC_BUF_TRC("buf_poolCreate_out",poolRef,poolRef->ptr,poolRef->len,-1);
  // 64BITS return (uint32_t)poolRef;
  return poolRef;
}

//#warning all the services of ruc_buffer are inline (see include file)
#if 0

// 64BITS uint32_t ruc_buf_poolRecover(uint32 poolRef)
uint32_t ruc_buf_poolRecover(void * poolRef)
{
  ruc_buf_t *phead;
  ruc_obj_desc_t  *pnext=(ruc_obj_desc_t*)NULL;
  ruc_buf_t  *p;
  uint32_t ret;


  phead = (ruc_buf_t*)poolRef;

  RUC_BUF_TRC("buf_poolRecover",poolRef,phead->ptr,-1,-1);

  if (phead->type != BUF_POOL_HEAD)
  {
    /*
    **  not a buffer pool reference
    */
    RUC_WARNING(phead->type);
    return RUC_NOK;
  }
  ret = ruc_listRecover((ruc_obj_desc_t *)phead,TRUE);
  if (ret == RUC_NOK)
  {
    /*
    **  unable to recover the buffer pool
    */
    RUC_WARNING(phead);
    return RUC_NOK;
  }
  /*
  ** change the buffer state to BUF_FREE
  */
   while ((p = (ruc_buf_t*)ruc_objGetNext((ruc_obj_desc_t*)phead,&pnext))
               !=(ruc_buf_t*)NULL)
   {
      p->state = BUF_FREE;
   }
   return RUC_OK;

}




/*
**    note : bufsize MUST be long word aligned
*/

// 64BITS uint32_t ruc_buf_poolCreate(uint32 nbBuf,uint32 bufsize)
void * ruc_buf_poolCreate(uint32_t nbBuf,uint32 bufsize)
{
  ruc_buf_t  *poolRef;
  ruc_obj_desc_t  *pnext=(ruc_obj_desc_t*)NULL;
  char *pusrData;
  char *pBufCur;
  ruc_buf_t  *p;

   RUC_BUF_TRC("buf_poolCreate",nbBuf,bufsize,-1,-1);
   /*
   **   create the control part of the buffer pool
   */
   poolRef = (ruc_buf_t*)ruc_listCreate(nbBuf,sizeof(ruc_buf_t));
   if (poolRef==(ruc_buf_t*)NULL)
   {
     /*
     ** cannot create the buffer pool
     */
     RUC_WARNING(-1);
     // 64BITS return (uint32_t)NULL;
     return NULL;
   }
   poolRef->type = BUF_POOL_HEAD;
   /*
   **  create the usrData part
   */
   /*
   **  bufsize MUST long word aligned
   */
   if ((bufsize & 0x3) != 0)
   {
     bufsize = ((bufsize & (~0x3)) + 4 );
   }
  /*
  ** test that the size does not exceed 32 bits
  */
  {
    uint32_t nbElementOrig;
    uint32_t NbElements;
    uint32_t memRequested;

    nbElementOrig = nbBuf;
    if (nbElementOrig == 0)
    {
      RUC_WARNING(-1);
      // 64BITS return (uint32_t)NULL;
      return NULL;
    }

    memRequested = bufsize*(nbElementOrig);
    NbElements = memRequested/(bufsize);
    if (NbElements != nbElementOrig)
    {
      /*
      ** overlap
      */
      RUC_WARNING(-1);
      // 64BITS return (uint32_t)NULL;
      return NULL;
    }
  }

   pusrData = (char*)memalign(32,bufsize*nbBuf);
   if (pusrData == (char *) NULL)
   {
     /*
     **  out of memory, free the pool
     */
     RUC_WARNING(-1);
     ruc_listDelete((ruc_obj_desc_t*)poolRef);
     // 64BITS return (uint32_t)NULL;
     return NULL;
   }
   /*
   ** store the pointer address on the head
   */
   poolRef->ptr = (uint8_t*)pusrData;
   poolRef->bufCount = nbBuf;
   poolRef->len = (uint32_t)nbBuf*bufsize;
   poolRef->usrLen = nbBuf;

   pBufCur = pusrData;
   /*
   ** init of the payload pointers
   */
   while ((p = (ruc_buf_t*)ruc_objGetNext((ruc_obj_desc_t*)poolRef,&pnext))
               !=(ruc_buf_t*)NULL)
   {
      p->ptr = (uint8_t*)pBufCur;
      p->state = BUF_FREE;
      p->bufCount  = (uint16_t)bufsize;
      p->type = BUF_ELEM;
      p->callBackFct = (ruc_pf_buf_t)NULL;
      pBufCur += bufsize;
   }
   /*
   **  return the reference of the buffer pool
   */
  RUC_BUF_TRC("buf_poolCreate_out",poolRef,poolRef->ptr,poolRef->len,-1);
  // 64BITS return (uint32_t)poolRef;
  return poolRef;
}



// 64BITS uint32_t ruc_buf_poolDelete(uint32 poolRef)
uint32_t ruc_buf_poolDelete(void * poolRef)
{
  ruc_buf_t *p;

  p = (ruc_buf_t*)poolRef;

  RUC_BUF_TRC("buf_poolDelete",poolRef,p->ptr,-1,-1);

  if (p->type != BUF_POOL_HEAD)
  {
    /*
    **  not a buffer pool reference
    */
    RUC_WARNING(p->type);
    return RUC_NOK;
   }
   /*
   **  free the usrData part of the pool
   */
   free ((char*)p->ptr);
   /*
   **  free the buffer list
   */
   ruc_listDelete((ruc_obj_desc_t*)p);
   return RUC_OK;
}

// 64BITS uint32_t ruc_buf_isPoolEmpty(uint32 poolRef)
uint32_t ruc_buf_isPoolEmpty(void * poolRef)
{
  ruc_buf_t *p;

  p = (ruc_buf_t*)poolRef;

  if (p->type != BUF_POOL_HEAD)
  {
    /*
    **  not a buffer pool reference
    */
    RUC_WARNING(p->type);
    return TRUE;
  }
  return (ruc_objIsEmptyList((ruc_obj_desc_t*)p));
}


// 64BITS uint32_t ruc_buf_getBuffer(uint32 poolRef)
void * ruc_buf_getBuffer(void * poolRef)
{
  ruc_buf_t *p,*pelem;

  p = (ruc_buf_t*)poolRef;
  pelem = (ruc_buf_t*)ruc_objGetFirst((ruc_obj_desc_t*)p);
  if (pelem == (ruc_buf_t* )NULL)
  {
    RUC_BUF_TRC("buf_getBuffer_NULL",poolRef,-1,-1,-1);
    // 64BITS return (uint32_t) NULL;
    return NULL;
  }
  /*
  **  remove the buffer from the list
  */
  ruc_objRemove((ruc_obj_desc_t*)pelem);
  pelem->state = BUF_ALLOC;
  /*
  ** set the current payload to 0
  */
  pelem->usrLen = 0;
  /*
  **update the current number of buffer in the buffer pool
  */
   p->usrLen--;
  /*
  ** clear the callback (normally this is done on delete
  */
  p->callBackFct = (ruc_pf_buf_t)NULL;
  // 64BITS p->callBackParam = 0;
  p->callBackParam = NULL;

  RUC_BUF_TRC("buf_getBuffer",poolRef,pelem->ptr,pelem,-1);
  // 64BITS return ((uint32_t)pelem);
  return (pelem);
}





// 64BITS uint32_t ruc_buf_freeBuffer(uint32 bufRef)
uint32_t ruc_buf_freeBuffer(void * bufRef)
{

  ruc_buf_t *phead;
  ruc_buf_t *pelem;

  RUC_BUF_TRC("buf_freeBuffer",bufRef,-1,-1,-1);
  pelem = (ruc_buf_t*)bufRef;
  /*
  **  check if it is a buffer element
  */
  if (pelem->type != BUF_ELEM)
  {
    RUC_WARNING(pelem->type);
    return RUC_NOK;
  }
  /*
  ** get the reference of the pool
  ** from buffer
  */
  phead = (ruc_buf_t*)ruc_objGetHead((ruc_obj_desc_t*)pelem);
  if (phead == (ruc_buf_t*)NULL)
  {
    /*
    **  not a buffer pool reference
    */
    RUC_WARNING(-1);
    return RUC_NOK;
  }
  /*
  **  check if the buffer has be already released
  */
  if (pelem->state == BUF_FREE)
  {
    /*
    ** reject the service
    */
    RUC_WARNING(pelem);
    return RUC_NOK;
  }
  /*
  ** set buffer state to FREE
  */
  pelem->state = BUF_FREE;

  /*
  **  test if there is a callback function attached to
  **  the buffer. In that case call it before putting
  **  back the buffer in the free list
  */
  if (pelem->callBackFct != (ruc_pf_buf_t)NULL)
  {
     (*pelem->callBackFct)(pelem->callBackParam);
  }

  ruc_objInsertTail((ruc_obj_desc_t*)phead,(ruc_obj_desc_t*)pelem);

  /*
  **update the current number of buffer in the buffer pool
  */
   phead->usrLen++;

  return RUC_OK;
}

// 64BITS char * ruc_buf_getPayload(uint32_t bufRef)
char * ruc_buf_getPayload(void * bufRef)
{
  ruc_buf_t *pelem;

  pelem = (ruc_buf_t*)bufRef;
  if (pelem->type != BUF_ELEM)
  {
    /*
    ** unlucky guy!!
    */
    RUC_WARNING(bufRef);
    return (char*)NULL;
  }
  if (pelem->state != BUF_ALLOC)
  {
    /*
    ** unlucky guy!!
    */
    RUC_WARNING(bufRef);
    return (char*)NULL;
  }

  return (char*)pelem->ptr;
}



// 64BITS uint32_t ruc_buf_getPayloadLen(uint32 bufRef)
uint32_t ruc_buf_getPayloadLen(void * bufRef)
{
  ruc_buf_t *pelem;

  pelem = (ruc_buf_t*)bufRef;
  if (pelem->type != BUF_ELEM)
  {
    /*
    ** unlucky guy!!
    */
    RUC_WARNING(bufRef);
    return 0;
  }
  if (pelem->state != BUF_ALLOC)
  {
    /*
    ** unlucky guy!!
    */
    RUC_WARNING(bufRef);
    return 0;
  }
  return pelem->usrLen;
}


// 64BITS uint32_t ruc_buf_setPayloadLen(uint32 bufRef,uint32 len)
uint32_t ruc_buf_setPayloadLen(void * bufRef,uint32 len)
{
  ruc_buf_t *pelem;

  pelem = (ruc_buf_t*)bufRef;
  if (pelem->type != BUF_ELEM)
  {
    /*
    ** unlucky guy!!
    */
    RUC_WARNING(bufRef);
    return RUC_NOK;
  }
  if (pelem->state != BUF_ALLOC)
  {
    /*
    ** unlucky guy!!
    */
    RUC_WARNING(bufRef);
    return RUC_NOK;
  }
  pelem->usrLen = len;
  return RUC_OK;
}

/*
**   That function permits to associate a call back function
**   that will be called when the buffer is released.
**
**    INPUT:
**          buffer reference
**          call back function
**          input parameter of the callback function.
*/


// 64BITS uint32_t ruc_buf_setCallBack(uint32 bufRef,ruc_pf_buf_t pfct, uint32 par)
uint32_t ruc_buf_setCallBack(void * bufRef,ruc_pf_buf_t pfct, void * par)
{
  ruc_buf_t *pelem;

  pelem = (ruc_buf_t*)bufRef;
  if (pelem->type != BUF_ELEM)
  {
    /*
    ** unlucky guy!!
    */
    RUC_WARNING(bufRef);
    return RUC_NOK;
  }
  if (pelem->state != BUF_ALLOC)
  {
    /*
    ** unlucky guy!!
    */
    RUC_WARNING(bufRef);
    return RUC_NOK;
  }
  pelem->callBackFct = pfct;
  pelem->callBackParam = par;

  return RUC_OK;
}


// 64BITS uint32_t ruc_buf_getFreeBufferCount(uint32 poolRef)
uint32_t ruc_buf_getFreeBufferCount(void * poolRef)
{
  ruc_buf_t *p;

  p = (ruc_buf_t*)poolRef;

  if (p->type != BUF_POOL_HEAD)
  {
    /*
    **  not a buffer pool reference
    */
    RUC_WARNING(p->type);
    return 0;
  }
  return(p->usrLen);
}


// 64BITS uint32_t ruc_buf_getInitBufferCount(uint32 poolRef)
uint32_t ruc_buf_getInitBufferCount(void * poolRef)
{
  ruc_buf_t *p;

  p = (ruc_buf_t*)poolRef;

  if (p->type != BUF_POOL_HEAD)
  {
    /*
    **  not a buffer pool reference
    */
    RUC_WARNING(p->type);
    return 0;
  }
  /*
  **
  */
  return(p->bufCount);
}
#endif
