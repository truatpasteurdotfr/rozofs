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

#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>

#include "ruc_list.h"
#include "ruc_trace_api.h"


/*
**  G L O B A L   D A T A
*/
uint32_t ruc_curListIdentifier= 0;
uint32_t ruc_list_trace = TRUE;
int ruc_list_shmid_table[4096];


/*----------------------------------------
   P R I V A T E    A P I
-----------------------------------------*/

void ruc_list_set_trace(uint32_t flag)
{
  ruc_list_trace = flag;
}



/*
**__________________________________________________________
*/
/**
ruc_obj_desc_t *ruc_listCreate_shared(uint32_t nbElements,uint32 size)

   creation of a double linked list. The input arguments
    are the number of elements and the size of an element.

   it is mandatory that the element includes ruc_obj_desc_t
    at the beginning of its structure.

   @param  nbElements : number of elements to create
   @param  size  : size of the structure of an element (including the size of ruc_obj_desc_t).
   @param key: key of the shared memory

   @retval <> NULL: pointer to the head of list
   @retval == NULL: out of memory

  note : the number of elements must not include the head of
         list.
*/
ruc_obj_desc_t *ruc_listCreate_shared(uint32_t nbElements,uint32_t size,key_t key)
{

  ruc_obj_desc_t *p,*phead;
  uint32_t   listId;
  uint8_t    *pbyte;
  int i;
  int shmid;

  RUC_LIST_TRC("listCreate_in_shared",nbElements,size,-1,-1);
  /*
  **  reject the creation if the size is less than the
  **  ruc_obj_desc_t structure size
  */
  if (size < sizeof(ruc_obj_desc_t))
  {
    RUC_WARNING(-1);
    return (ruc_obj_desc_t*)NULL;
  }
  /*
  **  if the size is not long word aligned, adjust the size
  **  to do it.
  */
  if ((size & 0x3) != 0)
  {
     size = ((size & (~0x3)) + 4 );
  }
  /*
  ** test that the size does not exceed 32 bits
  */
  {
    uint32_t nbElementOrig;
    uint32_t memRequested;

    nbElementOrig = nbElements+1;
    if (nbElementOrig == 0)
    {
      RUC_WARNING(-1);
      return (ruc_obj_desc_t*)NULL;
    }

    memRequested = size*(nbElementOrig);
    nbElementOrig = memRequested/size;
    if (nbElementOrig != (nbElements+1))
    {
      /*
      ** overlap
      */
      RUC_LIST_TRC("listCreate_err",nbElementOrig,nbElements,-1,-1);
      RUC_WARNING(-1);
      return (ruc_obj_desc_t*)NULL;
    }
  }
  /*
  ** create the shared memory
  */
  if ((shmid = shmget(key, size*(nbElements+1), IPC_CREAT | 0666)) < 0) {
      perror("shmget");
      RUC_WARNING(errno);
      return (ruc_obj_desc_t*)NULL;
  }
  /*
  * Now we attach the segment to our data space.
  */
  if ((p =(ruc_obj_desc_t *) shmat(shmid, NULL, 0)) == (ruc_obj_desc_t *) -1)
  {
    perror("shmat");
    RUC_WARNING(errno);
    return (ruc_obj_desc_t*)NULL;
  }

  /*
  ** get the list Id for the new list
  */
  listId = ruc_getListId();
  /*
  ** store the reference of the shared memory
  */
  ruc_list_shmid_table[listId] = shmid;
  /*
  ** head of list initialization
  */
  phead = p;
  phead->ps     = phead;
  phead->pp     = phead;
  phead->sysRef = p;
  phead->type   = RUC_LIST_HEAD;
  phead->countOrObjId = nbElements ;
  phead->eltSize =  size;
  phead->usrEvtCode = 0;
  phead->listId = listId;

  pbyte = (uint8_t*)p;
  for (i = 0; i < nbElements; i++)
  {
    pbyte +=size;
    p = (ruc_obj_desc_t*)pbyte;
    /*
    **  initialize the element header
    */
    ruc_listEltInit(p);
    p->sysRef = phead;
    p->type   = RUC_LIST_ELEM;
    p->listId = phead->listId;
     /*
     **  insert in the list
     */
    ruc_objInsertTail(phead,p);
  }
  RUC_LIST_TRC("listCreate_out",nbElements,size,phead,phead->listId);
  return phead;
}



/*
**__________________________________________________________
*/
/**
int32_t ruc_listDelete_shared(ruc_obj_desc_t *phead)


    release a double linked list that has been created
    by using ruc_listCreate_shared().
    It releases the memory
    it is mandatory that the element includes ruc_obj_desc_t
    at the beginning of its structure.
 :
    @param   phead :pointer to the head of list

    :
    @retval    RUC_OK : the list has been released
    @retval    RUC_NOK: one of the following errors has
                 been encountered:
          - the list has not been created by ruc_listCreate()
          - phead is not a head of list element.


  note : the function does not control that all the elements
         have been returned to the list. If some elements are
         queued to some other lists, memory corruption can
         occur.

*/

uint32_t ruc_listDelete_shared(ruc_obj_desc_t *phead)
{
  uint32_t ret;
  int shmid;
  /*
  **  check that phead is a head of list
  */
  RUC_LIST_TRC("listDelete_in",phead,-1,-1,-1);
  if (phead->type != RUC_LIST_HEAD)
  {
    RUC_WARNING(phead->type);
    return RUC_NOK;
  }
  /*
  **  check that all the elements are in the linked list
  **  note : that control is optional. It is based of the
  **         control of the list Identifier
  */
  ret = ruc_listCheck(phead);
  if (ret != TRUE)
  {
    RUC_WARNING(-1);
    /*
    ** there is some elements that are not in the queue
    ** or queued to another queue
    */
    ret = ruc_listRecover(phead,TRUE);
    if (ret != TRUE)
    {
      /*
      ** unable to recover the elements or the recover
      ** optional is not active
      */
      RUC_WARNING(-1);
      return RUC_NOK;
    }
  }
  /*
  **  all is fine, clear the type and free the memory
  */
  phead->type = 0;
  /*
  ** get the reference of the shared memory
  */
  shmid = ruc_list_shmid_table[phead->listId];
  /*
  ** detach from the shared memory
  */
  if (shmdt(phead)< 0)
  {
    RUC_WARNING(errno);
  }
  /*
  ** delete it
  */
  if(shmctl(shmid,IPC_RMID,NULL) < 0)
  {
    RUC_WARNING(errno);
  }
  return RUC_OK;
}


