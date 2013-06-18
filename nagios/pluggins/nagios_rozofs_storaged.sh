#!/bin/bash
#set -x
TMPFILE=/tmp/storaged_rozodebug.$$
TMPFILE2=/tmp/storaged_rozodebug2.$$
VERSION="Version 1.0"
PROGNAME=`basename $0`

# Exit codes
STATE_OK=0
STATE_WARNING=1
STATE_CRITICAL=2
STATE_UNKNOWN=3

ROZODEBUG_PATHS=". /usr/bin /usr/local/bin $ROZO_TESTS/build/src/debug" 
resolve_rozodebug() {

  option="-i $host -p $port"

  if [ ! -z "$time" ];
  then
    option=`echo "$option -t $time"`
  fi

  for path in $ROZODEBUG_PATHS
  do
    if [ -f $path/rozodebug ];
    then
      ROZDBG="$path/rozodebug $option"
      return
    fi  
  done
  echo "Can not find rozodebug"
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
  echo "    Destination host of the Rozofsmount instance"
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
  port=50027
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
  port=`expr $port + 1 `
  resolve_rozodebug
  $ROZDBG -c profiler >  $TMPFILE
 
  res=`grep GPROFILER $TMPFILE`
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
	-H | --hostname)  scan_value $1 $2;         host=$value;       shift 2;;	
	-instance)          scan_numeric_value $1 $2; instance=$value;     shift 2;;	
	-t | --time)      scan_numeric_value $1 $2; time=$value;       shift 2;;
	-p)               scan_numeric_value $1 $2; port=$value;       shift 2;;
       *) display_output $STATE_UNKNOWN "Unexpected parameter or option $1"
   esac
done


# Check mandatory parameters are set

if [ -z $host ];
then
   display_output $STATE_UNKNOWN "-H option is mandatory"
fi  




# ping the destination host

ping $host -c 1 >> /dev/null
if [ $? != 0 ]
then
  # re attempt a ping
  ping $host -c 2 >> /dev/null
  if [ $? != 0 ]
  then  
    display_output $STATE_CRITICAL "$host do not respond to ping"
  fi 
fi


# Find rozodebug utility and prepare command line parameters

resolve_rozodebug


# Run vfstat_stor debug command on export to check storage status

$ROZDBG -c profiler >  $TMPFILE
res=`grep "storaged:" $TMPFILE`
case $res in
  "") {
    display_output $STATE_CRITICAL "$host do not respond to rozodebug"
  };;  
esac

# get the number of I/O processes
nb_out=0
nb_io_process=`awk '{if ($1=="storaged:") printf("%d\n",$4);}' $TMPFILE`
	for i in $(seq ${nb_io_process}); 
    do
      test_storage_io
      nb_out=`expr $nb_out + $? `
    done

if [ $nb_out -eq 0 ]
then
  display_output $STATE_CRITICAL "no I/O process is UP"
fi
if [ $nb_out -ne $nb_io_process ]
then
  display_output $STATE_WARNING "only $nb_out/$nb_io_process I/O process(es) are UP"
fi

# Hurra !!!
display_output $STATE_OK 
