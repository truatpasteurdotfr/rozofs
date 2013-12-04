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

#define RUCCOM_WARNING_C


/*
 *------------------------------------------------------------------
 *
 * INCLUDES FILES:
 *
 *------------------------------------------------------------------
*/

#include <stdio.h>
#include <stdlib.h>

#include <rozofs/common/types.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>

#include "ruc_common.h"

/*
 *------------------------------------------------------------------
 *
 * DEFINITIONS:
 *
 *------------------------------------------------------------------
*/

#define RUC_MAX_TRACE_FILENAME (12+16)
typedef struct _ruc_trace_bin_t {
	uint8_t		appId[RUC_MAX_TRACE_FILENAME ];
	uint64_t		par1;
	uint64_t		par2;
	uint64_t		par3;
	uint64_t		par4;

} RUCCOM_TRACE_BIN_T;

#define RUC_MAX_TRACE_BUF (sizeof(RUCCOM_TRACE_BIN_T))

#define RUC_MAX_WARN_FILENAME (RUC_MAX_TRACE_BUF-12)
typedef struct _ruc_trace_warn_t {
	uint8_t		file[RUC_MAX_WARN_FILENAME];
	uint32_t		line;
	uint64_t		err;

} RUCCOM_TRACE_WARN_T;

typedef struct _ruc_trace_t {
	uint16_t		idx;
	uint16_t		type;
	uint8_t		buf[RUC_MAX_TRACE_BUF ];
} RUCCOM_TRACE_T;


#define RUCCOM_MAX_TRC	256

/*
 *------------------------------------------------------------------
 *
 * GLOBAL VARIABLE DEFINITIONS:
 *
 *------------------------------------------------------------------
*/

RUCCOM_TRACE_T 	*ruc_bufTrace_p=0;

static	uint16_t		ruc_warnIdx;
static	uint16_t		ruc_warnElt;
static	uint16_t	ruc_fatalLock;
uint32_t          ruc_tracePrintFlag = FALSE;
uint8_t           ruc_bufAll[128];
uint8_t           ruc_bufAll2[128];




/*
 *------------------------------------------------------------------
 *
 * EXTERNAL FUNCTION DECLARATIONS:
 *
 *------------------------------------------------------------------
 */

/*
 *------------------------------------------------------------------
 *
 * LOCAL VARIABLE DECLARATIONS:
 *
 *------------------------------------------------------------------
 */
/*static char *myFile = __FILE__;*/
#define myFile __FILE__


/*
 *------------------------------------------------------------------
 *
 * INTERNAL FUNCTION DECLARATIONS:
 *
 *------------------------------------------------------------------
 */

 /*------------------------------------------------------------------
 *
 * DEFINITIONS:
 *
 *------------------------------------------------------------------
*/




/*.FUNCTION - <DBG_FATAL>
 *------------------------------------------------------------------
 *
 * NAME: ruc_fatal
 *
 *------------------------------------------------------------------
 *
 * DESCRIPTION: save in the trace buffer the filename and the line
 *
 *------------------------------------------------------------------
 *
 * CALLING SEQUENCE:
 *
 *------------------------------------------------------------------
 *------------------------------------------------------------------
 *
 * INPUTS:  char *source
 *	    int	  line
 *
 *------------------------------------------------------------------
 *
 * OUTPUTS: nothing
 *
 *------------------------------------------------------------------
 *
 * NOTES:
 *
 *------------------------------------------------------------------
 */


/*.FUNCTION - <ruc_warning>
 *------------------------------------------------------------------
 *
 * NAME: ruc_warning
 *
 *------------------------------------------------------------------
 *
 * DESCRIPTION: save in the trace buffer the filename and the line
 *
 *------------------------------------------------------------------
 *
 * CALLING SEQUENCE:
 *
 *------------------------------------------------------------------
 *------------------------------------------------------------------
 *
 * INPUTS:  char *source
 *	    int	  line
 *
 *------------------------------------------------------------------
 *
 * OUTPUTS: nothing
 *
 *------------------------------------------------------------------
 *
 * NOTES:
 *
 *------------------------------------------------------------------
 */



uint32_t
ruc_warning(char *source, uint32_t line,uint32_t errCode)



{
	int             j;
	RUCCOM_TRACE_WARN_T *p;
	RUCCOM_TRACE_T	*q;
	uint8_t	*pdst,data;


       severe("$W$ File: %s:%d (%d) $E$\n",(char*)source,(int) line,(int)errCode);

        if (ruc_tracePrintFlag == TRUE)
        {
#if 0
          ruc_printErr(&ruc_bufAll[0],errCode);
#endif
	  printf("$W$ File: %s \t Line:%d %s(%d) $E$\n",(char*)source,(int) line,ruc_bufAll,(int)errCode);
        }

	if (ruc_bufTrace_p == 0) {
		return errCode;
	}
	if (ruc_warnElt >= RUCCOM_MAX_TRC) ruc_warnElt = 0;
	q = ruc_bufTrace_p+ruc_warnElt;
	p = (RUCCOM_TRACE_WARN_T* )&q->buf[0];
	pdst = &p->file[0];
	j=0;
	while ((*source != '\0')
	       && (j < RUC_MAX_WARN_FILENAME )) {
		if ((data = *source++)=='/') j = 0;
		else pdst[j++] = data;
	}
	pdst[j] = '\0';
	p->line = line;
        p->err  = errCode;
	q->idx = ruc_warnIdx++;
	q->type = 02;
	ruc_warnElt++;

       return errCode;

}


/*.FUNCTION - <ruc_trace>
 *------------------------------------------------------------------
 *
 * NAME: ruc_trace
 *
 *------------------------------------------------------------------
 *
 * DESCRIPTION: save in the trace buffer the filename and the line
 *
 *------------------------------------------------------------------
 *
 * CALLING SEQUENCE:
 *
 *------------------------------------------------------------------
 *------------------------------------------------------------------
 *
 * INPUTS:  char *source
 *	    int	  line
 *
 *------------------------------------------------------------------
 *
 * OUTPUTS: nothing
 *
 *------------------------------------------------------------------
 *
 * NOTES:
 *
 *------------------------------------------------------------------
 */

void
ruc_trace(char *appId, uint64_t par1,uint64_t par2,uint64_t par3,uint64_t par4)



{
	int             j;
	RUCCOM_TRACE_BIN_T *p;
	RUCCOM_TRACE_T	*q;
	uint8_t	*pdst;


        if (ruc_tracePrintFlag == TRUE)
        {
#if 0
	  printf("%s(%llu,%llu,%llu,%llu)\n",(char*)appId,par1,par2,par3,par4);
#else
	  printf("$T$ %s %llu %llu %llu %llu $E$\n",(char*)appId,(long long unsigned int)par1,
                                                (long long unsigned int)par2,(long long unsigned int)par3, 
                                                (long long unsigned int)par4);

#endif
        }
	if (ruc_bufTrace_p == 0) {
	  return;
	}

	if (ruc_warnElt >= RUCCOM_MAX_TRC) ruc_warnElt = 0;
	q = ruc_bufTrace_p + ruc_warnElt;
	p = (RUCCOM_TRACE_BIN_T* )&q->buf[0];
	pdst = &p->appId[0];
	for (j= 0;j < RUC_MAX_TRACE_FILENAME ; j++) {
		*pdst++ = *appId++;
	}
	*pdst = '\0';
	p->par1 = par1;
	p->par2 = par2;
	p->par3 = par3;
	p->par4 = par4;


	q->idx = ruc_warnIdx++;
	q->type = 01;
	ruc_warnElt++;


}



/*.FUNCTION - <DBG_ERRBUFINIT>
 *------------------------------------------------------------------
 *
 * NAME: ruc_errBufInit
 *
 *------------------------------------------------------------------
 *
 * DESCRIPTION: save in the trace buffer the filename and the line
 *
 *------------------------------------------------------------------
 *
 * CALLING SEQUENCE:
 *
 *------------------------------------------------------------------
 *------------------------------------------------------------------
 *
 * INPUTS:  char *source
 *	    int	  line
 *
 *------------------------------------------------------------------
 *
 * OUTPUTS: nothing
 *
 *------------------------------------------------------------------
 *
 * NOTES:
 *
 *------------------------------------------------------------------
 */
void ruc_traceBufInit()
{
	int	i,j;
	RUCCOM_TRACE_T *p;
/*	register	char	*pdst,data; jmm */
	uint8_t	*pdst;


	ruc_warnIdx = 0;
	ruc_warnElt =0 ;
      /*
      **  allocate the trace buffer
      */
    	ruc_bufTrace_p = (RUCCOM_TRACE_T*)malloc(sizeof(RUCCOM_TRACE_T)*RUCCOM_MAX_TRC);
      if (ruc_bufTrace_p == (RUCCOM_TRACE_T*)NULL)
       return;

	for(i=0;i<RUCCOM_MAX_TRC;i++) {
		p = &ruc_bufTrace_p[i];
		p->idx = 0;
		p->type = 0;
		pdst = &p->buf[0];
		for (j= 0;j<RUC_MAX_TRACE_BUF ;j++) pdst[j] = 0;
	}


	ruc_fatalLock= 0;
        ruc_tracePrintFlag = FALSE;
}





/*
void ruc_warning(char *filename,int line)
{
	printf ("\nFile: %s \t Line:%d",filename,line);
}
*/


/*.FUNCTION - <STPNI_PRINTOFF/STPNI_PRINTON>
 *------------------------------------------------------------------
 *
 * NAME: ruc_printoff/ruc_printon
 *
 *------------------------------------------------------------------
 *
 * DESCRIPTION: turn off and on the printf
 *
 *------------------------------------------------------------------
 *
 * CALLING SEQUENCE:
 *
 *------------------------------------------------------------------
 *------------------------------------------------------------------
 *
 * INPUTS:  char *source
 *	    int	  line
 *
 *------------------------------------------------------------------
 *
 * OUTPUTS: nothing
 *
 *------------------------------------------------------------------
 *
 * NOTES:
 *
 *------------------------------------------------------------------
 */
void ruc_printoff()
{
	ruc_tracePrintFlag = FALSE;
}

void ruc_printon()
{
	ruc_tracePrintFlag = TRUE;

}

/*.FUNCTION - <ruc_traceprint>
 *------------------------------------------------------------------
 *
 * NAME: ruc_traceprint
 *
 *------------------------------------------------------------------
 *
 * DESCRIPTION: print the event trace of the Spanning Tree (does not
 *              include the messages of the Socket Handler)
 *
 *------------------------------------------------------------------
 *
 * CALLING SEQUENCE:
 *
 *------------------------------------------------------------------
 *------------------------------------------------------------------
 *
 * INPUTS:  char *source
 *	    int	  line
 *
 *------------------------------------------------------------------
 *
 * OUTPUTS: nothing
 *
 *------------------------------------------------------------------
 *
 * NOTES:
 *
 *------------------------------------------------------------------
 */
void ruc_traceprint()
{
	int             i;
	RUCCOM_TRACE_T	*q;
	RUCCOM_TRACE_BIN_T *pBin;
	RUCCOM_TRACE_WARN_T *pWarn;


	printf (" TRACE BEGIN \n");

	if (ruc_bufTrace_p == 0) {
		printf("No trace buffer allocated \n");
		printf (" TRACE END \n");
		return;
	}
	for (i = 0; i <RUCCOM_MAX_TRC; i++) {
		q = ruc_bufTrace_p + i;
	    	switch(q->type) {
			case 1:
		 	/*------------------------------
		 	   event trace
			-------------------------------*/
			pBin = (RUCCOM_TRACE_BIN_T* )&q->buf[0];
			printf("%4d - %s (%llu,%llu,%llu,%llu)\n",
				q->idx,
				pBin->appId,
				(long long unsigned int)pBin->par1,
				(long long unsigned int)pBin->par2,
				(long long unsigned int)pBin->par3,
				(long long unsigned int)pBin->par4);
			break;

	     		case 2:

		   	/*-----------------------------
		      	 warning
		  	 ------------------------------*/
			pWarn = (RUCCOM_TRACE_WARN_T* )&q->buf[0];
#if 0
                        ruc_printErr(&ruc_bufAll2[0],pWarn->err);
#endif
	                printf("%4d - Warning File: %s \t Line:%d %s(%d)\n",
                                 q->idx,
                                 (char*)pWarn->file,
                                 (int)  pWarn->line,
                                 ruc_bufAll2,
                                 (int)  pWarn->err);

			break;

			default:
			/* not valid type */
			break;

		}
	}
	printf (" TRACE END \n");
}

