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
# setup.sh will generates a full working rozofs locally
# it's a useful tool for testing and debugging purposes. 
#
. env.sh 2> /dev/null



########################################## 
# RUN ELEMENTARY TESTS WHILE ONE STORAGE
# IS FAILED
##########################################
do_one_storage_failure() {
   echo "***** Stop storage $1 *****"
  ./setup.sh storage $1 stop
  sleep 3
  $2   
  res=$?
  ./setup.sh storage $1 start  
  return $res
}  
OneStorageFailed () {

# without storage 1
  do_one_storage_failure 1 $1
  if [ $? != 0 ];
  then
    return $?
  fi
  
# without storage 2
  do_one_storage_failure 2 $1
  if [ $? != 0 ];
  then
    return $?
  fi
  
# without storage 3
  do_one_storage_failure 3 $1
  if [ $? != 0 ];
  then
    return $?
  fi
  
# without storage 4
  do_one_storage_failure 4 $1
  if [ $? != 0 ];
  then
    return $?
  fi
  
  return 0  
}
########################################## 
# RUN ELEMENTARY TESTS WHILE RESETTING THE
# STORAGES ONE AFTER THE OTHER
##########################################
StorageReset_process () {

  sid=1
  while [ 1 ];
  do
  
    echo "***** Reset storage $sid *****"
    ./setup.sh storage $sid reset
    
    sid=$((sid+1))
    if [ "$sid" == "5" ];
    then
      sid=1
    fi

    sleep 7
  done
}  
StorageReset () {

  # Start process that reset the storages
  StorageReset_process &

  # Execute the test
  $1   
  res=$?

  # kill the storage reset process
  kill  $!  2> /dev/null
  wait

  # Return reset
  return $res
}
########################################## 
# RUN ELEMENTARY TESTS WHILE RESETTING THE
# STOCLI PERIODICALY
##########################################
do_storcli_reset ()  {
  for process in `ps -ef  | grep storcli | grep -v rozofsmount | grep -v grep | awk '{print $2}'`
  do
    echo "***** Reset storcli $process *****"
    kill -9 $process
  done
}
StorcliReset_process () {

  while [ 1 ];
  do  
    sleep 7
    do_storcli_reset
  done
}  
StorcliReset () {

  # Start process that reset the storcli
  StorcliReset_process &

  # Execute the test
  $1   
  res=$?

  # kill the storage reset process
  kill  $!  2> /dev/null
  wait

  # Return reset
  return $res
}
#################### ELEMENTARY TESTS ##################################
rw_close () {
  printf "process=%d loop=%d fileSize=%d\n" $process $loop $fileSize
  ./rw -process $process -loop $loop -fileSize $fileSize
  return $?
}
rw_noClose () {
  printf "process=%d loop=%d fileSize=%d\n" $process $loop $fileSize
  ./rw -process $process -loop $loop -fileSize $fileSize -noclose
  return $?
}

prepare_file_to_read() {
  if [ ! -f $1 ];
  then
    dd if=/dev/zero of=$1 bs=1M count=$fileSize
    return
  fi
  size=`ls -sh $1 | awk '{print $1}' | awk -F'M' '{print $1}'`
  if [ "$size" -ge "$fileSize" ];
  then
    return
  fi
  dd if=/dev/zero of=$1 bs=1M count=$fileSize    
}
read_parallel () {
  printf "process=%d loop=%d fileSize=%d\n" $process $loop $fileSize
  prepare_file_to_read mnt1/myfile 
  ./read_parallel -process $process -loop $loop -file mnt1/myfile 
  return $?
}
xattr () {
  printf "process=%d loop=%d \n" $process $loop 
  ./test_xattr -process $process -loop $loop -mount mnt1
  return $?  
}
link () {
  printf "process=%d loop=%d \n" $process $loop 
  ./test_link -process $process -loop $loop -mount mnt1
  return $?  
}
readdir() {
  printf "process=%d loop=%d \n" $process $loop 
  ./test_readdir -process $process -loop $loop -mount mnt1
  return $?  
}
rename() {
  printf "process=%d loop=%d \n" $process $loop 
  ./test_rename -process $process -loop $loop -mount mnt1
  return $?  
}
chmod() {
  printf "process=%d loop=%d \n" $process $loop 
  ./test_chmod -process $process -loop $loop -mount mnt1
  return $?  
}
truncate() {
  printf "process=%d loop=%d fileSize=%d\n" $process $loop $fileSize
  ./test_trunc -process $process -loop $loop -fileSize $fileSize -mount mnt1
  return $?  
}
############### USAGE ##################################
usage () {
  echo "$name -l"
  echo  "   Display the list of test"
  echo "$name [-process <nb>] [-loop <nb>] [-fileSize <nb>] <test name>"      
  echo "    Run test <test name>"
  echo "$name [-process <nb>] [-loop <nb>] [-fileSize <nb>] all"
  echo "    Run all tests"
  exit -1
}
#################### COMPILATION ##################################
compile_program () {
  echo "compile $1.c"
  gcc $1.c -o $1 -g
}
compile_programs () {
  echo
  while [ ! -z "$1" ];
  do
    if [ ! -f $1 ];
    then
      compile_program $1
    else 
      list=`ls -t $1.c $1`
      recent=`echo $list | awk '{print $1}'`
      case "$recent" in
        "$1") ;;
	*)    compile_program $1;;
      esac	
    fi
    shift 1
  done
}
#################### ROZODEBUG CONTROLS ##################################
# rozodebug profiler command for storclis
rozodebug_stc_profiler_before()  {
  before=/tmp/stc_profiler.before
  
  ./dbg.sh stc profiler  > $before
}
rozodebug_stc_profiler_after()  {
  before=/tmp/stc_profiler.before
  after=/tmp/stc_profiler.after
  dif=/tmp/stc_profiler.dif

  ./dbg.sh stc profiler  > $after
  
  diff -bBwy --suppress-common-lines $before $after | grep -v "GPROFILER" | grep "prj_err\|prj_tmo" > $dif
  nbLine=`cat $dif | wc -l`
  if [ "$nbLine" == "0" ];
  then
    return 0
  fi
  printf "\n-------> differences detected on ./dbg.sh stc profiler !!!\n"
  cat $dif   
  return -1
}
# rozodebug storcli_buf command for storclis
rozodebug_storcli_buf_before()  {
  before=/tmp/storcli_buf.before
  
  ./dbg.sh stc storcli_buf  > $before
}
rozodebug_storcli_buf_after()  {
  before=/tmp/storcli_buf.before
  after=/tmp/storcli_buf.after
  dif=/tmp/storcli_buf.dif

  ./dbg.sh stc storcli_buf  > $after
  
  diff -bBwy --suppress-common-lines $before $after | grep -v "serialized" | grep -v EMPTY > $dif
  nbLine=`cat $dif | wc -l`
  if [ "$nbLine" == "0" ];
  then
    return 0
  fi
  printf "\n-------> differences detected on ./dbg.sh stc storcli_buf !!!\n"
  cat $dif   
  return -1
}
# rozodebug fuse command for rozofsmount
rozodebug_fs_fuse_before()  {
  before=/tmp/fuse.fs.before
  
  ./dbg.sh fs fuse |  grep FUSE > $before
}
rozodebug_fs_fuse_after()  {
  before=/tmp/fuse.fs.before
  after=/tmp/fuse.fs.after
  dif=/tmp/fuse.fs.dif

  ./dbg.sh fs fuse |  grep FUSE > $after
  
  diff -bBwy --suppress-common-lines $before $after > $dif
  nbLine=`cat $dif | wc -l`
  if [ "$nbLine" == "0" ];
  then
    return 0
  fi
  printf "\n-------> differences detected on ./dbg.sh fs fuse !!!\n"
  cat $dif   
  return -1
}
# rozodebug trx command for rozofsmount and storclis
rozodebug_trx_before()  {
  before=/tmp/trx.$1.before
  
  ./dbg.sh $1 trx > $before
}
rozodebug_trx_after()  {
  before=/tmp/trx.$1.before
  after=/tmp/trx.$1.after
  dif=/tmp/trx.$1.dif
  
  ./dbg.sh $1 trx > $after
  
  diff -bBwy --suppress-common-lines $before $after | grep -v TX_SEND | grep -v TX_RECV_OK  > $dif
  nbLine=`cat $dif | wc -l`
  if [ "$nbLine" == "0" ];
  then
    return 0
  fi
  printf "\n-------> differences detected on ./dbg.sh $1 trx !!!\n"
  cat $dif   
  return -1
}
# Saving rozodebug output before test
rozodebug_before () {
  rozodebug_trx_before fs
  rozodebug_trx_before stc
  rozodebug_fs_fuse_before  
  rozodebug_storcli_buf_before  
  rozodebug_stc_profiler_before  
}
# Checking modifcation of rozodebug output after test
rozodebug_after () {

  rozodebug_trx_after fs
  res1=$?

  rozodebug_trx_after stc
  res2=$?

  rozodebug_fs_fuse_after 
  res3=$?

  rozodebug_storcli_buf_after
  res4=$?

  rozodebug_stc_profiler_after
  res5=$?
  
  return $((res1+res2+res3+res4+res5))
}
#################### LIST THE TEST ##################################
build_all_test_list () {
  ALL_TST_LIST=""
  for TST in $TST_BASIC
  do
    ALL_TST_LIST=`echo "$ALL_TST_LIST $TST"` 
  done
  for TST in $TST_STORAGE_FAILED
  do
    ALL_TST_LIST=`echo "$ALL_TST_LIST $TST/OneStorageFailed"` 
  done
  for TST in $TST_STORAGE_RESET
  do
    ALL_TST_LIST=`echo "$ALL_TST_LIST $TST/StorageReset"` 
  done  
  for TST in $TST_STORCLI_RESET
  do
    ALL_TST_LIST=`echo "$ALL_TST_LIST $TST/StorcliReset"` 
  done    
}
list_tests () {
  idx=0
  for TST in $ALL_TST_LIST
  do
    idx=$((idx+1))
    printf " %3d) %s\n" $idx $TST 
  done
  exit -1
}
#################### RUNNING A LIST OF TEST ##################################
delay () {
  min=$1
  min=$((min/60))
  sec=$1
  sec=$((sec%60))
  zedelay=`printf "%d min %2.2d" $min $sec`
}
run_some_tests() {


  total_avant=`date +%s`

  FAIL=0
  SUSPECT=0
  printf "______________________________________________._________.____________.\n" >> $RESULT
  printf   "                 TEST NAME                    |  RESULT |  Duration  |\n" >> $RESULT
  printf "______________________________________________|_________|____________|\n" >> $RESULT

  for TST in $TSTS
  do
    printf "_____________________________________________\n"
    printf "Run $TST\n"
    printf ".............................................\n"  


    # Split the elementary test and the test conditions
    COND=`echo $TST | awk -F'/' '{print $2}'`
    FUNC=`echo $TST | awk -F'/' '{print $1}'`    
      
    # Save some rozodebug output before test    
    rozodebug_before
    avant=`date +%s`

    # Run the test according to the required conditions
    $COND $FUNC
    res_tst=$?  
          
    # Check some rozodebug output after the test    
    apres=`date +%s`
    delay $((apres-avant))    
    rozodebug_after    
    res_dbg=$?
    
    
    
    if [ $res_tst != 0 ];
    then
      # The test returns an error code
      printf "\n-------> !!! $TST is failed ($zedelay) !!!\n"
      printf "%45s | FAILED  | %10s |\n" $TST "$zedelay" >> $RESULT
      FAIL=$((FAIL+1))
      break
    else          
      if [ $res_dbg != 0 ];
      then
        # The test do not complain but some debug ouput change need to be checked 
        SUSPECT=$((SUSPECT+1))
        printf "\n-------> $TST suspicion of failure ($zedelay) !\n"
	printf "%45s | SUSPECT | %10s |\n" $TST "$zedelay" >> $RESULT
      else
        # The test is successfull
        printf "\n-------> $TST success ($zedelay)\n"      
	printf "%45s | OK      | %10s |\n" $TST "$zedelay" >> $RESULT  
      fi
    fi
  done

  total_apres=`date +%s`
  delay $((total_apres-total_avant))
  
  printf "______________________________________________|_________|____________|\n" >> $RESULT
  printf "                  FAILED TEST NUMBER          |  %5d  | %s\n" $FAIL "$zedelay" >> $RESULT
  printf "                 SUSPECT TEST NUMBER          |  %5d  |\n" $SUSPECT >> $RESULT
  printf "______________________________________________|_________|\n" >> $RESULT
  printf "(Check rozodebug outputs of SUSPECT tests to decide whether it successfull or failed)\n" >> $RESULT
  
  if [ $FAIL != 0 ];
  then
    return -1
  fi
  return 0  
}

name=`basename $0`
RESULT=/tmp/result
printf "\n" > $RESULT

# Default test dimensiooning
if [ -d /mnt/hgfs/windows ];
then
# VEHEM OUERE
  fileSize=20
  loop=16
  process=6
else
  fileSize=200
  loop=80
  process=12
fi  

# List of test
TST_BASIC="readdir xattr link rename chmod truncate read_parallel rw_close rw_noClose"
TST_STORAGE_FAILED="read_parallel rw_close rw_noClose"
TST_STORAGE_RESET="read_parallel rw_close rw_noClose"
TST_STORCLI_RESET="read_parallel rw_close rw_noClose"
build_all_test_list
TSTS=""


# Read parameters
while [ ! -z $1 ];
do
  case "$1" in
  
    # List the tests
    -l|-list)  list_tests;;

    # Dimensionning 
    -process)  process=$2;             shift 2;;
    -loop)     loop=$2;                shift 2;;
    -fileSize) fileSize=$2;            shift 2;;

    # Debugging this tool
    -verbose)  set -x;                 shift 1;;

    # help
    -*)        usage;;  

    # Read test list to execute
    all)       TSTS=$ALL_TST_LIST;     shift 1;;
    *)         TSTS=`echo "$TSTS $1"`; shift 1;;
  esac  
done

# No test requested
if [ "$TSTS" == "" ];
then
  usage
fi
  
# VEHEM OUERE
if [ -d /mnt/hgfs/windows ];
then
  # Set big READ timers to prevent from false read errors
  ./dbg.sh stc tmr_set 14 1000
fi  

# Compile programs
compile_programs rw read_parallel test_xattr test_link test_write test_readdir test_rename test_chmod test_trunc

# Kill export gateway
./setup.sh expgw all stop

# execute the tests
run_some_tests 
res=$?
cat $RESULT
exit $res
