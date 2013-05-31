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
#ifndef VSTK_OBJ_H
#define VSTK_OBJ_H


#include <stdlib.h>
#include <stdint.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <rozofs/common/types.h>

#include "ruc_common.h"
#include "ruc_trace_api.h"

#define iPPU 1    /**< MANDATORY FOR 64bits !!!! */

//#define RUC_LIST_DEBUG_MODE 1

#ifdef RUC_LIST_DEBUG_MODE
#warning ruc_list in debug mode
#endif
/*
**    DEFINITION
*/

#define RUC_LIST_HEAD   0x41   /* HEAD of list created by ruc_list */
#define RUC_LIST_HEAD_VD 0x42  /* HEAD of list of an user created list */
#define RUC_LIST_ELEM 0x43     /* list element */


/*
**   STRUCTURES
*/


/*
**   head of list: (RUC_LIST_HEAD)
**
**  sysRef : pointer returned by malloc
**  eltSize : size of an element of the list
**  countOrObjId : number of element created
**  type : RUC_LIST_HEAD
**
**   element of list: (RUC_LIST_ELEM)
**
**  sysRef : pointer to the head of list
**  eltSize : N.S (0)
**  countOrObjId : object identifier (for ordered insertion)
**  type : RUC_LIST_ELEM
**  usrEvtCode: user event code: used when different types
**              of message structures are put on the same
**              queue (the queue cannot be the discriminator).
**  listId : identifier of the list.
*/
typedef struct _ruc_obj_desc_t
{
  struct _ruc_obj_desc_t		*ps;
  struct _ruc_obj_desc_t		*pp;
  uint16_t 				eltSize;
  uint8_t		                        type;
  uint8_t                                 usrEvtCode;
#ifdef iPPU
  void *			        sysRef;
#else
  uint32_t			        sysRef;
#endif
  uint32_t 				countOrObjId;
  uint32_t                                listId;
#ifdef RUC_LIST_DEBUG_MODE
  char                  *filename;
#endif
}
ruc_obj_desc_t;


#define RUC_LIST_TRC(name,p1,p2,p3,p4) { if (ruc_list_trace==TRUE) \
                                          ruc_trace(name,(uint64_t)(long)p1,(uint64_t)(long)p2,(uint64_t)(long)p3,(uint64_t)(long)p4); }


/*
**  G L O B A L   D A T A
*/
extern uint32_t ruc_curListIdentifier;
extern uint32_t ruc_list_trace ;


/*
** PROTOTYPES
*/
/*
**   public API
*/
static inline ruc_obj_desc_t *ruc_listCreate(uint32_t nbElements, uint32_t size);
static inline uint32_t ruc_listDelete(ruc_obj_desc_t *phead);
static inline uint32_t ruc_listEltInit(ruc_obj_desc_t *p);
static inline void ruc_listHdrInit(ruc_obj_desc_t *phead);

ruc_obj_desc_t *ruc_listCreate_shared(uint32_t nbElements, uint32_t size, key_t key);
uint32_t ruc_listDelete_shared(ruc_obj_desc_t *phead);
static inline uint32_t ruc_objInsert(ruc_obj_desc_t *phead,ruc_obj_desc_t *pobj);
static inline uint32_t ruc_objInsertTail(ruc_obj_desc_t *phead,ruc_obj_desc_t *pobj);
static inline uint32_t ruc_objInsertOrdered(ruc_obj_desc_t *phead,ruc_obj_desc_t *pobj);
static inline uint32_t ruc_objRemove(ruc_obj_desc_t *pobj);
static inline uint32_t ruc_objIsEmptyList(ruc_obj_desc_t *phead);
static inline ruc_obj_desc_t *ruc_objGetFirst(ruc_obj_desc_t *phead);
static inline ruc_obj_desc_t *ruc_objGetNext(ruc_obj_desc_t *phead,
                                 ruc_obj_desc_t **pstart);
static inline ruc_obj_desc_t *ruc_objGetHead(ruc_obj_desc_t *pobj);
static inline ruc_obj_desc_t *ruc_objGetRefFromIdx(ruc_obj_desc_t *phead,uint32_t idx);
static inline uint32_t ruc_listMoveTail(ruc_obj_desc_t *pheadListToMove,
                        ruc_obj_desc_t *pheadDest);

/*
**  Queuing
*/
static inline uint32_t ruc_objPutQueue(ruc_obj_desc_t *phead,ruc_obj_desc_t *pobj,uint8_t evt);
static inline ruc_obj_desc_t *ruc_objReadQueue(ruc_obj_desc_t *phead,uint8_t *evt);
/*
**  Private API
*/
static inline uint32_t ruc_listCheck(ruc_obj_desc_t *phead);
static inline uint32_t ruc_listRecover(ruc_obj_desc_t *phead, uint32_t ReinitFlag);
static inline uint32_t ruc_listCheckHeader(ruc_obj_desc_t *phead);
static inline uint32_t ruc_listCheckObjFree(ruc_obj_desc_t *pobj);
void ruc_list_set_trace(uint32_t flag);


/*-------------------------------------------------
** uint32_t ruc_listRecover(ruc_obj_desc_t *phead)
**-------------------------------------------------
**  that function works for list created by
**  ruc_listCreate() only.
**  The purpose of thatfunction is to restore
**  the list as it is returned when ruc_listCreate()
**  was called.
**
**  Any element of the list queue on an other is
**  automatically removed
**
**   IN : phead : pointer to the head of list
**   OUT : RUC_OK : listis recovered
**         RUC_NOK : not done
**-------------------------------------------------
*/

static inline uint32_t ruc_listRecover(ruc_obj_desc_t *phead, uint32_t ReinitFlag)
{
  uint32_t i;
  uint32_t badThing;
  ruc_obj_desc_t *p;
  uint8_t *pbyte;
  uint32_t ret;

  if (phead->type != RUC_LIST_HEAD)
  {
    return RUC_NOK;
  }
  /*
  ** head of list initialization
  */
  p = phead;

  /*
  ** recover all the elements that are not queue
  ** on that original list
  */

  badThing = FALSE;
  pbyte = (uint8_t*)p;
  for (i = 0; i < phead->countOrObjId; i++)
  {
    pbyte +=phead->eltSize;
    p = (ruc_obj_desc_t*)pbyte;
    /*
    **  initialize the element header
    */
    if (p->listId != phead->listId)
    {
      /*
      ** remove it from the other list
      ** note : it is possible that the
      **        object does not belong to
      **        any list (listID = 0)
      */
      ret = ruc_objRemove(p);
      if (ret != RUC_OK)
      {
        /*
        ** something really wrong
        */
        badThing = TRUE;
        RUC_WARNING(p);
      }
      /*
      ** re-insert the object in its original list
      */
      ret = ruc_objInsert(phead,p);
      if (ret != RUC_OK)
      {
        /*
        ** something really wrong
        */
        badThing = TRUE;
        RUC_WARNING(p);
      }
    }
  }
  /*
  ** OK all the entries have been scanned
  ** if a reinitFlag is asserted re-do all
  ** the link list
  */
  if (ReinitFlag)
  {
     /*
     **  Not supported
     */
     if (badThing == TRUE) return  RUC_NOK;
     return RUC_OK;
  }
  if (badThing == TRUE) return RUC_NOK;
  return RUC_OK;
}


/*-------------------------------------------------
** uint32_t ruc_listCheckHeader(ruc_obj_desc_t *phead)
**-------------------------------------------------
**  that function works for list created by
**  ruc_listCreate() only.
**  The purpose of thatfunction is to check
**  the list as it is returned when ruc_listCreate()
**  was called.
**
**
**   IN : phead : pointer to the head of list
**   OUT : TRUE : head of list type
**         FALSE : not head of list element **-------------------------------------------------
*/



static inline uint32_t ruc_listCheckHeader(ruc_obj_desc_t *phead)
{
  if ((phead->type == RUC_LIST_HEAD) ||
      (phead->type == RUC_LIST_HEAD_VD))
    return TRUE;
  else
    return FALSE;
}


/*-------------------------------------------------
** uint32_t ruc_listCheck(ruc_obj_desc_t *phead)
**-------------------------------------------------
**  that function works for list created by
**  ruc_listCreate() only.
**  The purpose of thatfunction is to check
**  the list as it is returned when ruc_listCreate()
**  was called.
**
**
**   IN : phead : pointer to the head of list
**   OUT : 0 : all elts are in list
**         !0 : some are out side
**         -1 : big bug
**-------------------------------------------------
*/

static inline uint32_t ruc_listCheck(ruc_obj_desc_t *phead)
{
  uint32_t i;
  ruc_obj_desc_t *p;
  uint8_t *pbyte;
  uint32_t count = 0;

  if (phead->type != RUC_LIST_HEAD)
  {
    return -1;
  }
  /*
  ** head of list initialization
  */
  p = phead;

  /*
  ** recover all the elements that are not queue
  ** on that original list
  */

  pbyte = (uint8_t*)p;
  for (i = 0; i < phead->countOrObjId; i++)
  {
    pbyte +=phead->eltSize;
    p = (ruc_obj_desc_t*)pbyte;
    /*
    **  initialize the element header
    */
    if (p->listId != phead->listId)
    {
      count++;
    }
  }
  return count;
}



static inline uint32_t ruc_listCheckObjFree(ruc_obj_desc_t *pobj)
{
  if ((pobj->ps != pobj) || (pobj->pp != pobj))
    return FALSE;
  return TRUE;
}


static inline uint32_t ruc_getListId()
{
   ruc_curListIdentifier+=1;
   if (ruc_curListIdentifier == 0)
     ruc_curListIdentifier = 1;
   return ruc_curListIdentifier;
}
/*----------------------------------------
   P U B L I C    A P I
-----------------------------------------*/

static inline uint32_t ruc_listEltInit(ruc_obj_desc_t *p)
{

  p->ps = p;
  p->pp =p;
  p->sysRef = (void *) NULL;
  p->type   = RUC_LIST_ELEM;
  p->countOrObjId = 0 ; /* N.S */
  p->usrEvtCode = 0;
  p->eltSize = 0;
  p->listId = 0;

  return RUC_OK;

}

static inline uint32_t ruc_listEltInitAssoc(ruc_obj_desc_t *p,void *assoc_p)
{
  ruc_listEltInit( p);
  p->sysRef = (void *) assoc_p;
  return RUC_OK;

}

static inline void *ruc_listGetAssoc(ruc_obj_desc_t *p)
{

  return p->sysRef;

}
#ifndef RUC_LIST_DEBUG_MODE

static inline void ruc_listHdrInit(ruc_obj_desc_t *phead)
{
  phead->ps = phead;
  phead->pp = phead;
  phead->sysRef = (void *) NULL;
  phead->type   = RUC_LIST_HEAD_VD;
  phead->countOrObjId = 0 ; /* N.S */
  phead->usrEvtCode = 0;
  phead->eltSize  = 0;
  phead->listId = ruc_getListId();

}
#else
#define ruc_listHdrInit(p) _ruc_listHdrInit(p,__FILE__,__LINE__)
static inline void _ruc_listHdrInit(ruc_obj_desc_t *phead,char *file,int line)
{
  phead->ps = phead;
  phead->pp = phead;
  phead->sysRef = (void *) NULL;
  phead->type   = RUC_LIST_HEAD_VD;
  phead->countOrObjId = 0 ; /* N.S */
  phead->usrEvtCode = 0;
  phead->eltSize  = 0;
  phead->listId = ruc_getListId();
  phead->filename = malloc(strlen(file)+64);
  sprintf(phead->filename,"%s:%d",file,line);

}
#endif
/* #STARTDOC
**
**  #TITLE
ruc_obj_desc_t *ruc_listCreate(uint32_t nbElements,uint32 size)

**  #SYNOPSIS
**    creation of a double linked list. The input arguments
**    are the number of elements and the size of an element.
**
**    it is mandatory that the element includes ruc_obj_desc_t
**    at the beginning of its structure.
**
**   IN:
**       nbElements : number of elements to create
**       size  : size of the structure of an element (including
**               the size of ruc_obj_desc_t).
**
**   OUT :
**       <> NULL: pointer to the head of list
**       == NULL: out of memory
**
**  note : the number of elements must not include the head of
**         list.
**
** ##ENDDOC
*/

static inline ruc_obj_desc_t *ruc_listCreate(uint32_t nbElements, uint32_t size)
{

  ruc_obj_desc_t *p,*phead;
  uint32_t   listId;
  uint8_t    *pbyte;
  int i;

  RUC_LIST_TRC("listCreate_in",nbElements,size,-1,-1);
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
  ** alloc memory for building the list
  */
  p = (ruc_obj_desc_t *) malloc (size*(nbElements+1));
  if (p == (ruc_obj_desc_t *) NULL)
  {
    /*
    **  out of memory
    */
    RUC_WARNING(-1);
    return (ruc_obj_desc_t*) NULL;
  }

  /*
  ** get the list Id for the new list
  */
  listId = ruc_getListId();
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

/*----------------------------------------------------------
** #STARTDOC
**
**  #TITLE
int32_t ruc_listDelete(ruc_obj_desc_t *phead)

**  #SYNOPSIS
**    release a double linked list that has been created
**    by using ruc_listCreate().
**
**    It releases the memory
**
**    it is mandatory that the element includes ruc_obj_desc_t
**    at the beginning of its structure.
**
**   IN:
**       phead :pointer to the head of list
**
**   OUT :
**        RUC_OK : the list has been released
**        RUC_NOK: one of the following errors has
**                 been encountered:
**          - the list has not been created by ruc_listCreate()
**          - phead is not a head of list element.
**
**
**  note : the function does not control that all the elements
**         have been returned to the list. If some elements are
**         queued to some other lists, memory corruption can
**         occur.
**
** #ENDDOC
**---------------------------------------------------------------
*/

static inline uint32_t ruc_listDelete(ruc_obj_desc_t *phead)
{
  uint32_t ret;
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
  free((char*)phead);
  return RUC_OK;
}


/*----------------------------------------------------------
** #STARTDOC
**
**  #TITLE
uint32_t ruc_objInsert(ruc_obj_desc_t *phead,ruc_obj_desc_t *pobj)

**  #SYNOPSIS
**    Insert an element in a list. The input arguments are the
**    head of list pointer and the element that must be inserted
**
**   IN:
**       phead :pointer to the head of list
**       pobj : object to insert
**
**   OUT :
**        RUC_OK : the object has been inserted
**        RUC_NOK: the object has not been inserted for one
**                 of the following reasons:
**                   - phead is not an object header
**                   - pobj is already inserted in a list.
**
**
** ##ENDDOC
**---------------------------------------------------------------
*/
static inline uint32_t ruc_objInsert(ruc_obj_desc_t *phead,ruc_obj_desc_t *pobj)
{
   /*
   **  control on header and object
   */
#ifndef NO_PARANOIA
   if (ruc_listCheckHeader(phead)!= TRUE)
   {
     /*
     ** not a head of list
     */
     RUC_LIST_TRC("objInsert_err",phead,pobj,-1,-1);
     RUC_WARNING(phead);
     return RUC_NOK;
   }
   if (ruc_listCheckObjFree(pobj)!= TRUE)
   {
     /*
     ** the object is not free (still linked to another list
     */
     RUC_LIST_TRC("objInsert_err",phead,pobj,-1,-1);
     RUC_WARNING(pobj);
     return RUC_NOK;

   }
#endif
   pobj->ps = phead->ps;
   phead->ps->pp = pobj;
   phead->ps = pobj;
   pobj->pp = phead;
   /*
   **  update the listId on which the element has been
   **  linked
   */
  pobj->listId = phead->listId;
  return RUC_OK;

}

/*----------------------------------------------------------
** #STARTDOC
**
**  #TITLE
uint32_t ruc_objInsertTail(ruc_obj_desc_t *phead,ruc_obj_desc_t *pobj)

**  #SYNOPSIS
**    Insert an element in a list. The input arguments are the
**    head of list pointer and the element that must be inserted
**
**   IN:
**       phead :pointer to the head of list
**       pobj : object to insert
**
**   OUT :
**        RUC_OK : the object has been inserted
**        RUC_NOK: the object has not been inserted for one
**                 of the following reasons:
**                   - phead is not an object header
**                   - pobj is already inserted in a list.
**
**
** ##ENDDOC
**---------------------------------------------------------------
*/
static inline uint32_t ruc_objInsertTail(ruc_obj_desc_t *phead,ruc_obj_desc_t *pobj)
{
#ifndef NO_PARANOIA
   /*
   **  control on header and object
   */
   if (ruc_listCheckHeader(phead)!= TRUE)
   {
     /*
     ** not a head of list
     */
     RUC_LIST_TRC("objInsertTail_err",phead,pobj,-1,-1);
     RUC_WARNING(phead);
     return RUC_NOK;
   }
   if (ruc_listCheckObjFree(pobj)!= TRUE)
   {
     /*
     ** not free object
     */
     RUC_LIST_TRC("objInsertTail_err",phead,pobj,-1,-1);
     RUC_WARNING(pobj);
     return RUC_NOK;
   }
#endif
   pobj->ps = phead;
   phead->pp->ps = pobj;
   pobj->pp = phead->pp;
   phead->pp = pobj;
   /*
   **  update the listId on which the element has been
   **  linked
   */
  pobj->listId = phead->listId;
  return RUC_OK;

}




/*----------------------------------------------------------
** #STARTDOC
**
**  #TITLE
uint32_t ruc_objInsertOrdered(ruc_obj_desc_t *phead,ruc_obj_desc_t *pobj)

**  #SYNOPSIS
**    Insert an element in a list. The input arguments are the
**    head of list pointer and the element that must be inserted.
**    The countOrObjId field is used for that purpose and must
**    initialized by calling that service.
**
**   IN:
**       phead :pointer to the head of list
**       pobj : object to insert
**
**   OUT :
**        RUC_OK : the object has been inserted
**        RUC_NOK: the object has not been inserted for one
**                 of the following reasons:
**                   - phead is not an object header
**                   - pobj is already inserted in a list.
**
**
** ##ENDDOC
**---------------------------------------------------------------
*/

static inline uint32_t ruc_objInsertOrdered(ruc_obj_desc_t *phead,ruc_obj_desc_t *pobj)
{
   ruc_obj_desc_t *pnext;

   /*
   **  control on header and object
   */
   if (ruc_listCheckHeader(phead)!= TRUE)
   {
     /*
     ** not a head of list
     */
     RUC_LIST_TRC("objInsertOrdered_err",phead,pobj,-1,-1);
     RUC_WARNING(phead);
     return RUC_NOK;
   }
   if (ruc_listCheckObjFree(pobj)!= TRUE)
   {
     /*
     ** not a head of list
     */
     RUC_LIST_TRC("objInsertOrdered_err",phead,pobj,-1,-1);
     RUC_WARNING(pobj);
     return RUC_NOK;
   }

   pnext = phead;
   while (((pnext = pnext->ps) != phead) &&
          (pnext->countOrObjId < pobj->countOrObjId));
   pobj->ps = pnext;
   pnext->pp->ps = pobj;
   pobj->pp = pnext->pp;
   pnext->pp = pobj;

  pobj->listId = phead->listId;
  return RUC_OK;

}

/*----------------------------------------------------------
** #STARTDOC
**
**  #TITLE
uint32_t ruc_objRemove(ruc_obj_desc_t *pobj)

**  #SYNOPSIS
**    remove the element from a list.
**
**   IN:
**       pobj : object to remove
**
**   OUT :
**        RUC_OK : the object has been removed
**        RUC_NOK: the object to remove is not a list
**                 element.
**
**  note : if the element is already remove (ps==pp) then
**         no error is generated.(a warning might be generated
**         because this could hide a big bug).
**
**
** ##ENDDOC
**---------------------------------------------------------------
*/


static inline uint32_t ruc_objRemove(ruc_obj_desc_t *pobj)
{
  if (pobj->type != RUC_LIST_ELEM)
  {
    RUC_LIST_TRC("objRemove_err",pobj,pobj->type,pobj->listId,-1);
    RUC_WARNING(pobj);
    return RUC_NOK;
  }

  pobj->pp->ps = pobj->ps;
  pobj->ps->pp = pobj->pp;
  pobj->listId = 0;

  /*
  ** make it unlinked by setting ps=pp=pobj
  */
  pobj->ps = pobj;
  pobj->pp = pobj;

  return RUC_OK;
}

/*----------------------------------------------------------
** #STARTDOC
**
**  #TITLE
uint32_t ruc_objIsEmptyList(ruc_obj_desc_t *phead)

**  #SYNOPSIS
**    that function returns TRUE if the list is empty.
**
**   IN:
**       phead :pointer to the head of list
**
**   OUT :
**      TRUE : the list is empty
**
**  note : if the element is not a list header, the function
**         returns TRUE.
**
**
** ##ENDDOC
**---------------------------------------------------------------
*/


static inline uint32_t ruc_objIsEmptyList(ruc_obj_desc_t *phead)
{

#ifndef NO_PARANOIA
   /*
   ** check if it is a list header
   */
   if (ruc_listCheckHeader(phead)!=TRUE)
   {
     RUC_LIST_TRC("objIsEmptyList_err",phead,phead->type,-1,-1);
     RUC_WARNING(phead);
     return TRUE;
   }
#endif
   if (phead->ps == phead)
      return TRUE;
   return FALSE;
}


/*----------------------------------------------------------
** #STARTDOC
**
**  #TITLE
ruc_obj_desc_t *ruc_objGetByObjId(ruc_obj_desc_t *phead,uint32_t objId)

**  #SYNOPSIS
**    that service returns the element whose Id is objId.
**    If the object is not found, a NULL pointer is returned.
**    The element is not removed from the list.
**
**   IN:
**       phead :pointer to the head of list
**       objId : Object Identifier.
**
**   OUT :
**      NULL : not found
**     !=NULL : pointer to the object.
**
**  note : if phead is not a list header, the function
**         returns NULL.
**
**
** ##ENDDOC
**---------------------------------------------------------------
*/

static inline ruc_obj_desc_t *ruc_objGetByObjId(ruc_obj_desc_t *phead,uint32_t objId)
{

   ruc_obj_desc_t *pnext;

   /*
   ** check if it is a list header
   */
   if (ruc_listCheckHeader(phead)!=TRUE)
   {
     /*
     ** something is rotten
     */
     RUC_LIST_TRC("objGetByObjId_err",phead,objId,-1,-1);
     RUC_WARNING(phead);
     return (ruc_obj_desc_t*)NULL;
   }
   pnext = phead;
   while (((pnext = pnext->ps) != phead) && (pnext->countOrObjId != objId));
   if (pnext == phead)
     return (ruc_obj_desc_t*) NULL;
   return pnext;
}


/*----------------------------------------------------------
** #STARTDOC
**
**  #TITLE
ruc_obj_desc_t *ruc_objGetNext(ruc_obj_desc_t *phead,ruc_obj_desc_t **pstart)

**  #SYNOPSIS
**    that service returns the element that follows the one
**    specified in pstart. If pstart is a NULL pointer it returns
**    the first element of the list.
**    The element is not removed from the list.
**
**   IN:
**       phead :pointer to the head of list
**       pstart : pointer to the next element.
**
**   OUT :
**      NULL : empty queue
**     !=NULL : pointer to the object.
**    pstart : updated with the pointer to the next element.
**
**
**  note : if phead is not a list header, the function
**         returns NULL.
**         to start from the beginning of the list, pstart
**         must be set with NULL.
**
** ##ENDDOC
**---------------------------------------------------------------
*/

static inline ruc_obj_desc_t *ruc_objGetNext(ruc_obj_desc_t *phead,
                                 ruc_obj_desc_t **pstart)
{
   ruc_obj_desc_t *pnext;
#ifndef NO_PARANOIA

   if (ruc_listCheckHeader(phead)!=TRUE)
   {
     /*
     ** something is rotten
     */
     RUC_LIST_TRC("ruc_objGetNext_err",phead,pstart,-1,-1);
     RUC_WARNING(phead);
     return (ruc_obj_desc_t*)NULL;
   }
#endif
   if (*pstart == (ruc_obj_desc_t*)NULL)
      pnext = phead->ps;
   else
      pnext = *pstart;
   if (pnext == phead)
      return (ruc_obj_desc_t *) NULL;
   /*
   ** check if pnext is a valid element
   */
#ifndef NO_PARANOIA
   if (pnext->type != RUC_LIST_ELEM)
   {
     /*
     ** bad type
     */
     RUC_LIST_TRC("ruc_objGetNext_err",phead,pnext,pnext->type,-1);
     RUC_WARNING(pnext);
     return (ruc_obj_desc_t*)NULL;
   }
#endif
   *pstart = pnext->ps;
   return pnext;
}

/*----------------------------------------------------------
** #STARTDOC
**
**  #TITLE
ruc_obj_desc_t *ruc_objGetFirst(ruc_obj_desc_t *phead)

**  #SYNOPSIS
**    that service returns the first element from
**    the list. If the list is empty of a NULL pointer
**    is returned.
**    The element is not removed from the list.
**
**   IN:
**       phead :pointer to the head of list
**
**   OUT :
**      TRUE : the list is empty
**
**  note : if the element is not a list header, the function
**         returns TRUE.
**
**
** ##ENDDOC
**---------------------------------------------------------------
*/

static inline ruc_obj_desc_t *ruc_objGetFirst(ruc_obj_desc_t *phead)
{
   ruc_obj_desc_t *pnext;

#ifndef NO_PARANOIA
   if (ruc_listCheckHeader(phead)!=TRUE)
   {
     /*
     ** something is rotten
     */
     RUC_LIST_TRC("objGetFirst_err",phead,phead->type,-1,-1);
     RUC_WARNING(phead->type);
     return (ruc_obj_desc_t*)NULL;
   }
#endif
   pnext = phead->ps;
   if (pnext == phead)
      return (ruc_obj_desc_t *) NULL;
   return pnext;
}



/*----------------------------------------------------------
** #STARTDOC
**
**  #TITLE
ruc_obj_desc_t *ruc_objGetRefFromIdx(ruc_obj_desc_t *phead,uint32_t idx)

**  #SYNOPSIS
**    that service returns address of the object context based
**    on the index of the object.
**
**   IN:
**       phead :pointer to the head of list
**       idx : object index
**
**   OUT :
**      NULL if not found
**      !NULL if found
**
**  note : the list MUST have been created by using the
**         ruc_objListCreate() service.
**
**
** ##ENDDOC
**---------------------------------------------------------------
*/

static inline ruc_obj_desc_t *ruc_objGetRefFromIdx(ruc_obj_desc_t *phead,uint32_t idx)
{
  uint8_t *p;
#ifndef NO_PARANOIA
  if (phead->type != RUC_LIST_HEAD)
  {
    /*
    ** bad head of list or not initialized by ruc_objListCeate()
    */
    RUC_LIST_TRC("objGetRefFromIdx_err",phead,phead->type,idx,-1);
    RUC_WARNING(phead);
    return (ruc_obj_desc_t*)NULL;
  }
  /*
  **  get the max number of element of the list
  */
  if (idx >= phead->countOrObjId)
  {
    /*
    ** the index is out of order
    */
    RUC_LIST_TRC("objGetRefFromIdx_err",phead,idx,-1,-1);
    RUC_WARNING(idx);
    return (ruc_obj_desc_t*) NULL;
  }
#endif
  /*
  ** all is fine, get the pointer to the head of the list
  ** and compute the real address of the object
  */
  p = (uint8_t*) phead->sysRef;
  p +=(phead->eltSize*(idx+1));
  return (ruc_obj_desc_t*) p;

}


/*----------------------------------------------------------
** #STARTDOC
**
**  #TITLE
uint32_t ruc_listMoveTail(ruc_obj_desc_t *pheadListToMove,ruc_obj_desc_t *pheadDest)

**  #SYNOPSIS
**    Move all elements of a list at the end of the destination list.
**    The input arguments are the (pointer on the) head of the list to
**    move and the (pointer on the) head of the distinatory list.
**
**   IN:
**       pheadListToMove :pointer to the head of list to move
**       pheadDest : pointer to the head of the destinatory
**
**   OUT :
**        RUC_OK : the list has been moved
**        RUC_NOK: the list has not been moved for one
**                 of the following reasons:
**                   - pheadListToMove is not an object header
**                   - pheadDest is not an object header
**
**
** ##ENDDOC
**---------------------------------------------------------------
*/
static inline uint32_t ruc_listMoveTail(ruc_obj_desc_t *pheadListToMove,ruc_obj_desc_t *pheadDest)
{
   ruc_obj_desc_t *pfirstObjListToMove,*pobjPtr;

   /*
   **  control on header of list to move
   */
   if (ruc_listCheckHeader(pheadListToMove)!= TRUE)
   {
     /*
     ** not a head of list
     */
     RUC_LIST_TRC("ruc_listMoveTail/pheadListToMove",
     	pheadListToMove,pheadDest,-1,-1);
     RUC_WARNING(pheadListToMove);
     return RUC_NOK;
   }
   /*
   **  control on header of destinatory list
   */
   if (ruc_listCheckHeader(pheadDest)!= TRUE)
   {
     /*
     ** not a head of list
     */
     RUC_LIST_TRC("ruc_listMoveTail/pheadDest",
     	pheadListToMove,pheadDest,-1,-1);
     RUC_WARNING(pheadDest);
     return RUC_NOK;
   }
   /*
   ** check that there is at least one element to move
   */
   pfirstObjListToMove=pheadListToMove->ps;
   if(pfirstObjListToMove==pheadListToMove){
     return RUC_OK;
   }

   /*
   ** join beginning of list to move
   ** to end of destinatory list
   */
   pfirstObjListToMove->pp=pheadDest->pp;
   pheadDest->pp->ps=pfirstObjListToMove;
   /*
   ** join end of list to move
   ** to end-pointer of destinatory list
   */
   pheadListToMove->pp->ps=pheadDest;
   pheadDest->pp=pheadListToMove->pp;
   /*
   ** empty list to move
   */
   pheadListToMove->pp=pheadListToMove;
   pheadListToMove->ps=pheadListToMove;

   /*
   **  update the listId on which the element has been
   **  linked
   */
   pobjPtr=pfirstObjListToMove;
   while(pobjPtr!=pheadDest){
   	pobjPtr->listId = pheadDest->listId;
   	pobjPtr = pobjPtr->ps;
   }
  return RUC_OK;

}


/*----------------------------------------------------------
** #STARTDOC
**
**  #TITLE
ruc_obj_desc_t *ruc_objGetHead(ruc_obj_desc_t *pelem)

**  #SYNOPSIS
**    that service returns the pointer to the head of
**    list from which the element has been created
**
**    That service works for element of list that
**    have been created by using ruc_listCreate()
**
**   IN:
**       pelem :pointer to element from which the
**              the head of list has to be retrieved.
**
**   OUT :
**      NULL if not found
**      !NULL if found
**
**
** #ENDDOC
**---------------------------------------------------------------
*/

static inline ruc_obj_desc_t* ruc_objGetHead(ruc_obj_desc_t*pelem)
{
  if (pelem->type != RUC_LIST_ELEM)
  {
    /*
    ** bad element type
    */
    RUC_LIST_TRC("objGetHead_err",pelem,pelem->type,-1,-1);
    RUC_WARNING(pelem);
    return (ruc_obj_desc_t*)NULL;
  }
  if (pelem->sysRef == NULL)
  {
    /*
    ** not initialize by ruc_listCreate()
    */
    RUC_LIST_TRC("objGetHead_err",pelem,pelem->type,-1,-1);
    RUC_WARNING(pelem);
    return (ruc_obj_desc_t*)NULL;
  }
  return ((ruc_obj_desc_t*)pelem->sysRef);
}


/*----------------------------------------------------------
** #STARTDOC
**
**  #TITLE
uint32_t ruc_objPutQueue(ruc_obj_desc_t *phead,ruc_obj_desc_t *pobj,uint8_t evt);

**  #SYNOPSIS
**    that service associates an event with a list element
**    and queue it an the end of the list provided has
**    input argument (phead)
**
**
**   IN:
**       pelem :pointer to element from which the
**              the head of list has to be retrieved.
**
**   OUT :
** RUC_OK : element has been inserted
** RUC_NOK : nothing inserted
**
**
** #ENDDOC
**---------------------------------------------------------------
*/
static inline uint32_t ruc_objPutQueue(ruc_obj_desc_t *phead,ruc_obj_desc_t *pobj,uint8_t evt)
{
#ifndef NO_PARANOIA
  if (pobj->type != RUC_LIST_ELEM)
  {
    /*
    ** bad element type
    */
    RUC_LIST_TRC("objPutQueue_err",pobj,pobj->type,-1,-1);
    RUC_WARNING(pobj);
    return RUC_NOK;
  }

  if (ruc_listCheckHeader(phead)!= TRUE)
  {
    /*
    ** bad head of list or not initialized by ruc_objListCeate()
    */
    RUC_LIST_TRC("objPutQueue_err",phead,phead->type,-1,-1);
    RUC_WARNING(phead);
    return RUC_NOK;
  }
#endif
  pobj->usrEvtCode = evt;

  return( ruc_objInsertTail(phead,pobj));
}



/*----------------------------------------------------------
** #STARTDOC
**
**  #TITLE
ruc_obj_desc_t *ruc_objReadQueue(ruc_obj_desc_t *phead,uint8_t *evt);

**  #SYNOPSIS
**  read the first element of an event queue. When read, the
**  element is removed from the queue.
**
**
**   IN:

**
**   OUT :
**        NULL: the queue is empty
**      !=NULL : pointer to the element and evt contains the
**               user event.
**
**
** #ENDDOC
**---------------------------------------------------------------
*/
static inline ruc_obj_desc_t *ruc_objReadQueue(ruc_obj_desc_t *phead,uint8_t *evt)
{
  ruc_obj_desc_t *pelem;
#ifndef NO_PARANOIA
  if (ruc_listCheckHeader(phead)!= TRUE)
  {
    /*
    ** bad head of list or not initialized by ruc_objListCeate()
    */
    RUC_LIST_TRC("objReadQueue_err",phead,phead->type,-1,-1);
    RUC_WARNING(phead);
    return (ruc_obj_desc_t*)NULL;
  }
#endif
  pelem =(ruc_obj_desc_t *)ruc_objGetFirst(phead);
  if (pelem ==(ruc_obj_desc_t *)NULL)
  {
    /*
    ** empty queue
    */
    return (ruc_obj_desc_t *)NULL;
  }
  /*
  **  remove it from the list
  */
  ruc_objRemove(pelem);
  *evt = pelem->usrEvtCode;
  return pelem;
}




#endif
