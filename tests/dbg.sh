#!/bin/bash

#  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
#  This file is part of Rozofs.
#  Rozofs is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published
#  by the Free Software Foundation, version 2.
#  Rozofs is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see
#  <http://www.gnu.org/licenses/>.

#
# dbg.sh 
#
. env.sh


exp_hosts="localhost"

fs_hosts="localhost"

stc_hosts="localhost"

st1_host="localhost1"
st2_host="localhost2"
st3_host="localhost3"
st4_host="localhost4"
st_hosts="$st1_host $st2_host $st3_host $st4_host"

gw_hosts=$st_hosts
 
 
DBG_PORT_BASE="50000" 
set_debug_ports () {

  port=$DBG_PORT_BASE
  exp_ports=$port
  
  port=`expr $port + 1`
  gw_ports=$port

  # RBS
  port=`expr $port + 1`

  # FS
  fs_ports=""
  stc_ports=""
  for i in $(seq $1); 
  do
    port=`expr $port + 1`
    fs_ports=`echo "$fs_ports $port"`
    
    port=`expr $port + 1`
    stc_ports=`echo "$stc_ports $port"`
    port=`expr $port + 1`
    stc_ports=`echo "$stc_ports $port"`    
  done
  
  port=`expr $DBG_PORT_BASE + 27`
  std_ports=$port
  
  io_ports=""
  for i in $(seq $2); 
  do
    port=`expr $port + 1`
    io_ports=`echo "$io_ports $port"`  
  done  

} 
 
syntax() {
  echo "$name [verbose] [period <sec>] [exp|gw|fs|stc|st|std|io|all] [cmd1 [cmd2 [...]]]"
  exit
}
do_ask () {
 for h in $H
 do
   for p in $P
   do
     printf  "\n__[%8s]___[%10s:%-5s]" "$WHAT" $h $p   
     name=`${LOCAL_BINARY_DIR}/rozodebug/rozodebug -t 1 -i $h -p $p -c who | awk -F':' '{if ($1=="system ") print $2; else print " ??";}' | cut -b 2-`
     printf "__[%s]\n" "$name"
     ${LOCAL_BINARY_DIR}/rozodebug/rozodebug -t 1 -i $h -p $p $cmd
   done
 done
} 
ask_exp () {
  WHAT="EXPORT D"
  H=$exp_hosts
  P=$exp_ports
  do_ask
}
ask_gw () {
  WHAT="EXP GTWY"
  H=$gw_hosts
  P=$gw_ports
  do_ask
}
ask_fs () {
  WHAT="FS MOUNT"
  H=$fs_hosts
  P=$fs_ports
  do_ask
}
ask_stc () {
  WHAT="STOR CLI"
  H=$stc_hosts
  P=$stc_ports
  do_ask
}
ask_std () {
  WHAT="STORAGED"
  H=$st_hosts
  P=$std_ports
  do_ask
}
ask_io1 () {
  WHAT="STOR I/O"
  H=$st1_host
  P=$io_ports
  do_ask
}
ask_io2 () {
  WHAT="STOR I/O"
  H=$st2_host
  P=$io_ports
  do_ask
}
ask_io3 () {
  WHAT="STOR I/O"
  H=$st3_host
  P=$io_ports
  do_ask
}
ask_io4 () {
  WHAT="STOR I/O"
  H=$st4_host
  P=$io_ports
  do_ask
}
ask_io () {
  ask_io1
  ask_io2
  ask_io3
  ask_io4      
}
ask_st () {
  ask_std
  ask_io
}
ask_all () {
  ask_exp
  ask_gw
  ask_fs
  ask_stc
  ask_st
}
ask () {
  case "$who" in
   exp) ask_exp;; 
   gw)  ask_gw;;
   fs)  ask_fs;; 
   stc) ask_stc;; 
   st)  ask_st;; 
   std) ask_std;; 
   io)  ask_io;; 
   io1) ask_io1;; 
   io2) ask_io2;; 
   io3) ask_io3;;   
   io4) ask_io4;;   
   all) ask_all;;
  esac
}

name=`basename $0`

case "$1" in
  -*|?) syntax;;
esac  

if [ "$1" == "verbose" ];
then 
  set -x
  shift 1
fi

# 1 FS 4 STORIO
set_debug_ports 1 4

perio=""
if [ "$1" == "period" ];
then 
  period=$2
  shift 2
fi


case "$1" in
  exp|gw|fs|stc|st|std|io|io1|io2|io3|io4|all) who=$1; shift 1;;
  *)                        who=all;;
esac    
    
while [ ! -z "$1" ];
do
  cmd=`echo "$cmd $1"`
  shift 1
done  
case "$cmd" in
  "") cmd="-c uptime";;
  *)  cmd=`echo "-c $cmd"`;;
esac  

delta=0
ask  
if [ ! -z $period ];
then
  while [ 1 ];
  do
    sleep $period
    delta=`expr $delta + $period`
    echo 
    echo "~~~~~~~~~ +$delta sec ~~~~~~~~~"
    echo
    ask
  done
fi
