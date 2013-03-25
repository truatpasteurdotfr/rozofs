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
#ifndef __PPU_TRACE_H
#define __PPU_TRACE_H



#ifdef __cplusplus /*if we are compiling in c++*/
extern "C" {
#endif


/* API to put a trace in the trace file */
extern void erevd_trace(char * level,char *file,int line,char *fmt, ... );
extern void erevd_dump(char * level,char *file,int line,char *comm, char *pZone, int size );
void erevd_traceX(char *file,int line,char *fmt, ... ) ;
extern void erevd_event(int eventType,char *file, int line, char *fmt, ... );
extern void erevd_trace_init(char * process_name);
extern unsigned short          erevd_activated_level[];
extern unsigned short          erevd_activated_dump;

#ifdef __cplusplus /*if we are in c++*/
}
#endif


/*
  ____________________TRACING MACRO REDEFINITION FOR PPU_____________________
  INFO0   ... EINFO
  INFO1   ... EINFO
  ...
  INFO14  ... EINFO
  INFOLIB ... EINFO
  INFOX   ... EINFOX
  DUMP    ... EDUMP
  DUMPX   ... EDUMPX
*/
#ifdef INFO0
#undef INFO0
#endif
#ifdef INFO1
#undef INFO1
#endif
#ifdef INFO2
#undef INFO2
#endif
#ifdef INFO3
#undef INFO3
#endif
#ifdef INFO4
#undef INFO4
#endif
#ifdef INFO5
#undef INFO5
#endif
#ifdef INFO6
#undef INFO6
#endif
#ifdef INFO7
#undef INFO7
#endif
#ifdef INFO8
#undef INFO8
#endif
#ifdef INFO9
#undef INFO9
#endif
#ifdef INFO10
#undef INFO10
#endif
#ifdef INFO11
#undef INFO11
#endif
#ifdef INFO12
#undef INFO12
#endif
#ifdef INFO13
#undef INFO13
#endif
#ifdef INFO14
#undef INFO14
#endif
#ifdef INFOLIB
#undef INFOLIB
#endif
#ifdef INFOX
#undef INFOX
#endif
#ifdef DUMPX
#undef DUMPX
#endif
#ifdef DUMP
#undef DUMP
#endif
//#ifdef EINFO
//#undef EINFO
//#endif
#ifdef EINFOX
#undef EINFOX
#endif
#ifdef EDUMPX
#undef EDUMPX
#endif
#ifdef EDUMP
#undef EDUMP
#endif

/* Define non conditionnal macro traces */
#define INFO       erevd_trace("--",__FILE__,__LINE__,
//#define INFO_PRINT erevd_trace("PR",__FILE__,__LINE__,
#define INFO_PRINT printf(

/* Redefine EINFO, EINFOX, INFO0...INFO14, INFOLIB & INFOX */
#define INFO0  if(erevd_activated_level[0])  erevd_trace("00",__FILE__,__LINE__,
#define INFO1  if(erevd_activated_level[1])  erevd_trace("01",__FILE__,__LINE__,
#define INFO2  if(erevd_activated_level[2])  erevd_trace("02",__FILE__,__LINE__,
#define INFO3  if(erevd_activated_level[3])  erevd_trace("03",__FILE__,__LINE__, 
#define INFO4  if(erevd_activated_level[4])  erevd_trace("04",__FILE__,__LINE__, 
#define INFO5  if(erevd_activated_level[5])  erevd_trace("05",__FILE__,__LINE__,
#define INFO6  if(erevd_activated_level[6])  erevd_trace("06",__FILE__,__LINE__,
#define INFO7  if(erevd_activated_level[7])  erevd_trace("07",__FILE__,__LINE__,
#define INFO8  if(erevd_activated_level[8])  erevd_trace("08",__FILE__,__LINE__,
#define INFO9  if(erevd_activated_level[9])  erevd_trace("09",__FILE__,__LINE__,
#define INFO10 if(erevd_activated_level[10]) erevd_trace("10",__FILE__,__LINE__,
#define INFO11 if(erevd_activated_level[11]) erevd_trace("11",__FILE__,__LINE__,
#define INFO12 if(erevd_activated_level[12]) erevd_trace("12",__FILE__,__LINE__,
#define INFO13 if(erevd_activated_level[13]) erevd_trace("13",__FILE__,__LINE__,
#define INFO14 if(erevd_activated_level[14]) erevd_trace("14",__FILE__,__LINE__,
#define INFOLIB if(erevd_activated_dump)erevd_trace("LB",__FILE__,__LINE__,
#define INFOX erevd_traceX(__FILE__,__LINE__,
#define DUMP if(erevd_activated_dump)erevd_dump("DU",__FILE__,__LINE__,
#define DUMPX erevd_dump("DX",__FILE__,__LINE__,
#define RUC_EINFO );
#define EINFOX );
#define EDUMPX );
#define EDUMP );


/*
  ________________EVENT LOGGING MACRO REDEFINITION FOR PPU____________________
  EVT    ... EEVT
  ERRLOG ... ENDERRLOG
  ERRFAT ... ENDERRFAT
*/
#ifdef EVT
#undef EVT
#endif
#ifdef EEVT
#undef EEVT
#endif
#ifdef ERRLOG
#undef ERRLOG
#endif
#ifdef ENDERRLOG
#undef ENDERRLOG
#endif
#ifdef ERRFAT
#undef ERRFAT
#endif
#ifdef ENDERRFAT
#undef ENDERRFAT
#endif


#define EREVD_TYPE_FATAL_ERROR 0
#define EREVD_TYPE_SW_ERROR    1
#define EREVD_TYPE_EVENT       2

//#define ERRFAT erevd_event(EREVD_TYPE_FATAL_ERROR,__FILE__,__LINE__,
//#define ERRLOG erevd_event(EREVD_TYPE_SW_ERROR,__FILE__,__LINE__,

#define ERRFAT printf(
#define ERRLOG printf(
#define EVT    erevd_event(EREVD_TYPE_EVENT,__FILE__,__LINE__,
#define ENDERRLOG );
#define ENDERRFAT );
#define EEVT      );


/*
  ____________MSID & IMSI SPYING MACRO REDEFINITION FOR PPU_________________
  MS_SPY_IMSI ... E_MS_SPY
  MS_SPY_MSID ... E_MS_SPY

  The definition of these macro is unchanged for the PPU
*/

#endif
