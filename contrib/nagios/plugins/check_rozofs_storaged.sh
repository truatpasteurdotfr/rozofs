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
TMPFILE=/tmp/storaged_rozodiag.$$
TMPFILE2=/tmp/storaged_rozodiag2.$$
VERSION="Version 1.0"
PROGNAME=`basename $0`

# Exit codes
STATE_OK=0
STATE_WARNING=1
STATE_CRITICAL=2
STATE_UNKNOWN=3

rozodiag_PATHS=". /usr/bin /usr/local/bin $ROZO_TESTS/build/src/rozodiag" 
resolve_rozodiag() {

  if [ ! -z "$port" ];
  then
    option="-i $host -p $port"
  else
    option="-i $host -T $DBGTARGET"   
  fi

  if [ ! -z "$time" ];
  then
    option=`echo "$option -t $time"`
  fi

  for path in $rozodiag_PATHS
  do
    if [ -f $path/rozodiag ];
    then
      ROZDBG="$path/rozodiag $option"
      return
    fi  
  done
  echo "Can not find rozodiag"
  exit $STATE_UNKNOWN 
}

# Help functions
print_revision() {
   # Print the revision number
   echo "$PROGNAME - $VERSION"
}
print_usage() {
   # Print a short usage statement
   echo "Usage: $PROGNAME [-v] [-p <port>] [-t <seconds>] -H <host> "
}
print_help() {

   # Print detailed help information
   print_revision
   print_usage

  echo "Options:"
  echo "{-h|--help}"
  echo "    Print detailed help screen"
  echo "{-V|--version}"
  echo "    Print version information"
  echo "{-H|--hostname} <host name>"
  echo "    Destination host of the storage instance"
  echo "{-t|--timeout} <seconds>"
  echo "    Time out value"  
#  echo "{-w|--warning} <threshold|percent%>"
#  echo "    Exit with WARNING status if less than <threshold> GB or <percent>% of disk is free"
#  echo "{-c|--critical} <threshold|percent%>"
#  echo "    Exit with CRITICAL status if less than <threshold> GB or <percent>% of disk is free"
  echo "-p <port>"
  echo "    To eventually redefine the debug port"
  echo "{-v|--verbose}"
  echo "    Verbose output"
  exit $STATE_OK
}

display_output() {
  case $1 in
   "$STATE_OK")         msg="OK <> ";;
   "$STATE_WARNING")    msg="WARNING <$2> ";;
   "$STATE_CRITICAL")   msg="CRITICAL <$2> ";; 
   "$STATE_UNKNOWN")    msg="UNKNOWN <$2> ";;    
  esac
  
  msg=`echo "$msg | read=$all_read""B;;;; write=$all_write""B;;;;"`
  echo $msg
  rm -f $TMPFILE
  rm -f $TMPFILE2
  exit $1
}
set_default() {
  verbosity=0
  host=""
  time=""
  DBGTARGET="storaged"
}

is_numeric () {
  if [ "$(echo $1 | grep "^[ [:digit:] ]*$")" ] 
  then 
    value=$1
    return 1
  fi
  return 0
}
is_percent () {
  last=`echo $1 | tail -c2`
  if [ $last != '%' ] 
  then 
    return 0
  fi
      
  value=`echo $1 | sed 's/.\{1\}$//g'`
  is_numeric $value
  if [ $? = 0 ]
  then
    display_output $STATE_UNKNOWN "Percent value is not numeric"    
  fi
  if [ $value -gt 100 ];
  then
    display_output $STATE_UNKNOWN "Percent must not exceed 100"
  fi
  if [ $value -lt 0 ];
  then
    display_output $STATE_UNKNOWN "Percent must not be less than 0"
  fi
  return 1 
}

scan_numeric_value () {
  if [ -z "$2" ];
  then 
      display_output $STATE_UNKNOWN "Option $1 requires an argument"   
  fi
  is_numeric $2
  if [ $? = 0 ];
  then
      display_output $STATE_UNKNOWN "Option $1 requires a numeric argument"   	   	
  fi
  value=$2
}
scan_value () {
  if [ -z "$2" ];
  then 
      display_output $STATE_UNKNOWN "Option $1 requires an argument"   
  fi
  value=$2
}

test_storage_io()
{
  # 1st storage_io
  $ROZDBG -c profiler >  $TMPFILE
 
  res=`grep read $TMPFILE`
  case $res in
   "") {
     return 0
   };;  
  esac
  
  read=`awk '{if ($1=="read") print $9; }' $TMPFILE`
  write=`awk '{if ($1=="write") print $9; }' $TMPFILE`
  all_read=$((all_read+read))
  all_write=$((all_write+write))
  
  return 1
}
test_storage_io_devices()
{
  # rozodiag has lready been resolved in test_storage_io
  # resolve_rozodiag
 
  # Check device status
  $ROZDBG -c device >  $TMPFILE
  faulty_devices=`awk '{ if ($3=="faulty" && $4=="devices") print $5 }' $TMPFILE`
  case $faulty_devices in
    "") return 1;;
    *)  return 0;;
  esac 
}
test_one_storio() {
  
  test_storage_io
  if [ $? -eq 0 ]
  then
    display_output $STATE_CRITICAL "some I/O process do not respond to rozodiag"
  fi

  # Test whether devices are OK
  faulty_devices=""
  test_storage_io_devices
  if [ $? -eq 0 ]
  then
    display_output $STATE_WARNING "Some devices are faulty : $faulty_devices"
  fi
} 
test_storio_single() 
{
  if [ ! -z "$portbase" ];
  then
    port=$((portbase + 1))
  else
    DBGTARGET="storio:0"
  fi 
  resolve_rozodiag
  
  test_one_storio
}
test_storio_multiple()
{
  while [ ! -z $1 ];
  do
 
    cid=$1
    
    if [ ! -z "$portbase" ];
    then
      port=$((portbase + cid))
    else
      DBGTARGET="storio:$cid"
    fi 
    resolve_rozodiag
    
    test_one_storio
    
    shift 1
  done
}
#######################################################
#         M A I N
#######################################################

# Set default values
set_default
all_read=0
all_write=0

# Parse command line options
while [ "$1" ]; do
   case "$1" in
       -h | --help)           print_help;;
       -V | --version)        print_revision; exit $STATE_OK;;
       -v | --verbose)        set -x; verbosity=1; shift 1;;
       -w | --warning | -c | --critical) {
           if [ -z "$2" ];
	   then 
               display_output $STATE_UNKNOWN "Option $1 requires an argument"   
	   fi
	   is_percent $2
	   if [ $? = 1 ];
	   then
	     [[ "$1" = *-w* ]] && thresh_warn_percent=$value || thresh_crit_percent=$value  	       
	   else
	     is_numeric $2
	     if [ $? = 1 ];
	     then
	       [[ "$1" = *-w* ]] && thresh_warn=$value || thresh_crit=$value  	     
	     else
               display_output $STATE_UNKNOWN "Threshold must be integer or percentage"
             fi
	   fi
           shift 2
        };;
	-H | --hostname)  scan_value $1 $2;         hostname=$value;       shift 2;;	
	-instance)          scan_numeric_value $1 $2; instance=$value;     shift 2;;	
	-t | --time)      scan_numeric_value $1 $2; time=$value;       shift 2;;
	-p)               scan_numeric_value $1 $2; port=$value;       shift 2;;
       *) display_output $STATE_UNKNOWN "Unexpected parameter or option $1"
   esac
done

if [ ! -z "$port" ];
then
  portbase=$port
fi

# Check mandatory parameters are set

if [ -z $hostname ];
then
   display_output $STATE_UNKNOWN "-H option is mandatory"
fi  

# Split host list and make the list of good hosts and bad hosts 
hostnames=$(echo $hostname | tr "/" "\n")
goodhost=""
badhost=""
for host in $hostnames
do
  
  # ping the destination host
  ping $host -c 1 >> /dev/null
  if [ $? != 0 ]
  then
    # re attempt a ping
    ping $host -c 2 >> /dev/null
    if [ $? != 0 ]
    then  
      badhost=`echo "$badhost $host"`
      continue
    fi 
  fi   

  # Run profiler debug command to check storage status
  resolve_rozodiag
  $ROZDBG -c profiler >  $TMPFILE
  res=`grep "storaged:" $TMPFILE`
  case $res in
    "") {
      display_output $STATE_CRITICAL "$host do not respond to rozodiag"     
    };;  
  esac
  
  goodhost=`echo "$goodhost $host"`
done

# When no host responds to ping raise a critical alarm
# When only a few hosts do not repond to ping raise a warning later
case "$goodhost" in
  "") display_output $STATE_CRITICAL "Storaged host do not respond to ping"
esac


# Get 1rst good address
host=$(echo $goodhost | awk '{print $1}')
resolve_rozodiag

# get the number of I/O processes
$ROZDBG -c storio_nb >  $TMPFILE
storio_nb=`awk '{if($1=="storio_nb") print $3;}' $TMPFILE`
case $storio_nb in
  "") {
    display_output $STATE_CRITICAL "$host do not respond to rozodiag storio_nb"
  };;  
esac

mode=`awk '{if($1=="mode") print $3;}' $TMPFILE`
case "$mode" in
  "multiple") {
    cids=`awk '{if($1=="cids") { for (i=3;i<=NF;i++) printf("%d ",$i); printf("\n");}}' $TMPFILE`
    test_storio_multiple $cids
  };;
  *) test_storio_single;;
esac  

# When only a few hosts do not repond to ping raise a warning now
case "$badhost" in
  "") ;;
  *) display_output $STATE_CRITICAL "Storage host do not respond to ping on addresse(s) $badhost";;
esac  

# Hurra !!!
display_output $STATE_OK 
