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
. env.sh 2> /dev/null



fs_host="localhost"
nb_fs=1
nb_io=1

syntax() {
  echo "$name [verbose] [period <sec>] [exp|gw*|fs*|stc*|st*|std*|io*|all] [cmd]"
  echo ""
  echo "The parameters are positional"
  echo "verbose is just for debugging this script"
  echo "period  is used to ask for a periodic request"
  echo "Then comes the target of the request"
  echo "Then comes the command" 
  exit
}
do_ask () {
 for h in $H
 do
   for p in $P
   do
     printf  "\n__[%8s]___[%10s:%-5s]" "$WHAT" $h $p   
     name=`${LOCAL_BINARY_DIR}/rozodebug/rozodebug -t 10 -i $h -p $p -c who | awk -F':' '{if ($1=="system ") print $2; else print " ??";}' | cut -b 2-`
     printf "__[%s]\n" "$name"
     ${LOCAL_BINARY_DIR}/rozodebug/rozodebug -t 10 -i $h -p $p $cmd
   done
 done
} 
ask_exp () {
  WHAT="EXPORT D"
  H=$exp_host
  P=$exp_port
  do_ask
}
ask_fs_instance () {
  WHAT="FS MOUNT $instance"
  H=$fs_host
  P=${fs_ports[$instance]}
  do_ask
}
ask_fs () {
  for instance in $(seq $nb_fs); 
  do   
    ask_fs_instance
  done       
}
ask_stc_instance () {
  WHAT="STOR CLI $instance"
  H=$fs_host
  port=${fs_ports[$instance]}
  P="$((port+1)) $((port+2))" 
  do_ask
}
ask_stc () {
  for instance in $(seq $nb_fs); 
  do   
    ask_stc_instance
  done
}
ask_std_instance () {
  WHAT="STORAGED $instance"
  H=${st_host[$instance]}
  P=$std_port
  do_ask
}
ask_std () {
  for instance in $(seq $nb_st); 
  do   
    ask_std_instance
  done       
}
ask_gw_instance () {
  WHAT="EXP GTWY $instance"
  H="${gw_host[instance]}"
  P=$gw_port
  do_ask
}
ask_gw () {
  for instance in $(seq $nb_gw); 
  do   
    ask_gw_instance
  done  
}
ask_io_instance ()  {
  WHAT="STOR I/O $instance"
  H=${st_host[$instance]}
  P=$io_ports
  do_ask
}
ask_io () {
  for instance in $(seq $nb_st); 
  do   
    ask_io_instance
  done     
}
ask_st_instance () {
  ask_std_instance
  ask_io_instance
}
ask_st () {
  for instance in $(seq $nb_st); 
  do   
    ask_st_instance
  done   
}
ask_all () {
  ask_exp
  ask_gw
  ask_fs
  ask_stc
  ask_st
}
ask () {

  instance=${who: -1}

  case "$who" in
   exp) ask_exp;; 
   gw)  ask_gw;;
   gw*) ask_gw_instance;;
   fs)  ask_fs;; 
   fs*) ask_fs_instance;;
   stc) ask_stc;; 
   stc*)ask_stc_instance;; 
   std) ask_std;; 
   std*)ask_std_instance;;
   st)  ask_st;; 
   st*) ask_st_instance;;     
   io)  ask_io;; 
   io*) ask_io_instance;; 
   all) ask_all;;
  esac
}



#####################################
# M A I N    E N T R Y    P O I N T #
#####################################



name=`basename $0`

# Display usage
case "$1" in
  -*|?) syntax;;
esac  


# Verbose mode for debug
if [ "$1" == "verbose" ];
then 
  set -x
  shift 1
fi


# periodic requests 
period=""
if [ "$1" == "period" ];
then 
  period=$2
  shift 2
fi


DBG_PORT_BASE="50000" 

# Scan Export VIP in config file
exp_host=`cat ${LOCAL_CONF}/export.conf | grep "exportd_vip = " | awk -F'\"' '{print $2}'`
port=$DBG_PORT_BASE
exp_port=$port


# Scan export gateways in config file
hosts="zero "
for h in `cat ${LOCAL_CONF}/export.conf | grep "{gwid = " | awk -F'\"' '{print $2}'`
do
  hosts=`echo "$hosts $h"`
done  
declare -a gw_host=($hosts)  
nb_instances=${#gw_host[@]}
nb_gw=$((nb_instances-1)) 
port=$((port + 1))
gw_port=$port   

# FS
ports="zero"
port=$((port + 2))  
for i in $(seq $nb_fs); 
do
  ports=`echo "$ports $port"`
  port=$((port+3))   
done
declare -a fs_ports=($ports) 

# Scan storages in config file
hosts="zero "
for h in `cat ${LOCAL_CONF}/export.conf | grep "{sid = " | awk -F'\"' '{print $2}'`
do
  hosts=`echo "$hosts $h"`
done  
declare -a st_host=($hosts)  
nb_instances=${#st_host[@]}
nb_st=$((nb_instances-1))
port=$((DBG_PORT_BASE + 27))
std_port=$port  
io_ports=""
for i in $(seq $nb_io); 
do
  port=$((port + 1))
  io_ports=`echo "$io_ports $port"`    
done  

 
# Get the targeted process
case "$1" in
  exp|gw*|fs*|stc*|std*|io*|st*|all) who=$1; shift 1;;
  *)                        who=all;;
esac    
   
cmd=""   
while [ ! -z "$1" ];
do
  cmd=`echo "$cmd $1"`
  shift 1
done  
case "$cmd" in
  "");;
  *)  cmd=`echo "-c $cmd"`;;
esac  

delta=0
ask  
if [ ! -z $period ];
then
  while [ 1 ];
  do
    sleep $period
    delta=$((delta + period))
    echo 
    echo "~~~~~~~~~ +$delta sec ~~~~~~~~~"
    echo
    ask
  done
fi
