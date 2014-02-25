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

storageFailed () {

  for sid in $(seq $NB_SID) 
  do

    echo -ne "Stop storage $sid : "
    ./setup.sh storage $sid stop
    sleep 3
    $1  
    ./setup.sh storage $sid start 
    sleep 3
    if [ $result != 0 ];
    then
      return
    fi
  done
  return   
}
########################################## 
# RUN ELEMENTARY TESTS WHILE RESETTING THE
# STORAGES ONE AFTER THE OTHER
##########################################
storageReset_process () {

  while [ 1 ];
  do
    for sid in $(seq $NB_SID)  
    do
      echo -ne "Reset storage $sid\033[0K\r"
      ./setup.sh storage $sid reset
      sleep 7
    done
  done
}  
storageReset () {
  sleep 4
 
  # Start process that reset the storages
  storageReset_process &
 

  # Execute the test
  # Process more loop to have more reset during test
  saveloop=$loop
  loop=$((loop*5))
  $1   
  loop=$saveloop

  # kill the storage reset process
  kill  $!  2> /dev/null
  wait 2> /dev/null

}
########################################## 
# RUN ELEMENTARY TESTS WHILE RESETTING THE
# STOCLI PERIODICALY
##########################################
do_StorcliReset ()  {
  for process in `ps -ef  | grep "storcli -i 1" | grep -v storcli_starter.sh | grep -v grep | awk '{print $2}'`
  do
    kill -9 $process
  done
}
StorcliReset_process () {
  nbreset=0  
  while [ 1 ];
  do  
    nbreset=$((nbreset+1))
    echo -ne "Reset storcli $nbreset\033[0K\r"
    do_StorcliReset
    sleep 7
  done
}  
StorcliReset () {
  sleep 3

  # Start process that reset the storcli
  StorcliReset_process &
  
  # Execute the test
  # Process more loop to have more reset during test
  saveloop=$loop
  loop=$((loop*2))
  $1   
  loop=$saveloop   

  # kill the storage reset process
  kill  $!  2> /dev/null
  wait 2> /dev/null
}
#################### ELEMENTARY TESTS ##################################
wr_rd_total () {
  ./rw -process $process -loop $loop -fileSize $fileSize -total
  result=$?
}
wr_rd_partial () {
  ./rw -process $process -loop $loop -fileSize $fileSize -partial
  result=$?
}
wr_rd_random() {
  ./rw -process $process -loop $loop -fileSize $fileSize -random
  result=$?
}
wr_rd_total_close () {
  ./rw -process $process -loop $loop -fileSize $fileSize -total -closeAfter
  result=$?
}
wr_rd_partial_close () {
  ./rw -process $process -loop $loop -fileSize $fileSize -partial -closeAfter
  result=$?
}
wr_rd_random_close() {
  ./rw -process $process -loop $loop -fileSize $fileSize -random -closeAfter
  result=$?
}
wr_close_rd_total () {
  ./rw -process $process -loop $loop -fileSize $fileSize -total -closeBetween
  result=$?
}
wr_close_rd_partial () {
  ./rw -process $process -loop $loop -fileSize $fileSize -partial -closeBetween
  result=$?
}
wr_close_rd_random() {
  ./rw -process $process -loop $loop -fileSize $fileSize -random -closeBetween
  result=$?
}
wr_close_rd_total_close () {
  ./rw -process $process -loop $loop -fileSize $fileSize -total -closeBetween -closeAfter
  result=$?
}
wr_close_rd_partial_close () {
  ./rw -process $process -loop $loop -fileSize $fileSize -partial -closeBetween -closeAfter
  result=$?
}
wr_close_rd_random_close() {
  ./rw -process $process -loop $loop -fileSize $fileSize -random -closeBetween -closeAfter
  result=$?
}
prepare_file_to_read() {
  if [ ! -f $1 ];
  then
    dd if=/dev/zero of=$1 bs=1M count=$fileSize
    return
  fi
  size=`ls -sh $1 | awk '{print $1}' | awk -F'M' '{print $1}'| awk -F',' '{print $1}'`
  if [ "$size" -ge "$fileSize" ];
  then
    return
  fi
  dd if=/dev/zero of=$1 bs=1M count=$fileSize    
}
rw2 () {
  count=$((loop*16))
  ./rw2 -loop $count -file mnt1_1/ze_rw2_test_file 
  result=$?    
}
read_parallel () {
  prepare_file_to_read mnt1_1/myfile 
  ./read_parallel -process $process -loop $loop -file mnt1_1/myfile 
  result=$?    
}
xattr () {
  ./test_xattr -process $process -loop $loop -mount mnt1_1
  result=$?    
}
link () {
  ./test_link -process $process -loop $loop -mount mnt1_1
  result=$?    
}
readdir() {
  ./test_readdir -process $process -loop $loop -mount mnt1_1
  result=$?    
}
rename() {
  ./test_rename -process $process -loop $loop -mount mnt1_1
  result=$?    
}
chmod() {
  ./test_chmod -process $process -loop $loop -mount mnt1_1
  result=$?    
}
truncate() {
  ./test_trunc -process $process -loop $loop -fileSize $fileSize -mount mnt1_1
  result=$?    
}
lock_posix_passing() {
  file=mnt1_1/lock
  unlink $file
  ./test_file_lock -process $process -loop $loop -file $file -nonBlocking
  result=$?    
}
lock_posix_blocking() {
  file=mnt1_1/lock
  unlink $file
  ./test_file_lock -process $process -loop $loop -file $file 
  result=$?    
}
lock_bsd_passing() {
  file=mnt1_1/lock
  unlink $file
  ./test_file_lock -process $process -loop $loop -file $file -nonBlocking -bsd
  result=$?    
}
lock_bsd_blocking() {
  file=mnt1_1/lock
  unlink $file
  ./test_file_lock -process $process -loop $loop -file $file -bsd
  result=$?    
}
############### USAGE ##################################
usage () {
  echo "$name -l"
  echo  "   Display the list of test"
  echo "$name [-process <nb>] [-loop <nb>] [-fileSize <nb>] [-repeated <times>] <test name1> <test name2>..."      
  echo "    Run the given test list, and repeat the run <times> times (default 1 time)"
  echo "    -process <nb>  gives the number of processes that will run the test in paralell. (default $process)"
  echo "    -loop <nb>     is the number of loop that each process will do. (default $process)"
  echo "    -fileSize <nb> is the size in MB of the file for the test. (default $fileSize)"   
  echo "    The name of the tests can be listed with -l"
  echo "     - all              designate all the tests."
  echo "     - rw               designate the read/write test list."
  echo "     - storagedFailed   designate the read/write test list run when a sid is failed."
  echo "     - storagedReset    designate the read/write test list run while a sid is reset."
  echo "     - storcliReset     designate the read/write test list run while the storcli is reset."
  echo "     - basic            designate the non read/write test list."
  exit -1 
}
#################### COMPILATION ##################################
compile_program () {
  echo "compile $1.c"
  gcc $1.c -lpthread -o $1 -g
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
  printf "! ! ! ! ! ! ! differences detected on ./dbg.sh stc profiler !!!\n"
  cat $dif   
  return 1
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
  printf "! ! ! ! ! ! ! differences detected on ./dbg.sh stc storcli_buf !!!\n"
  cat $dif   
  return 1
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
  printf "! ! ! ! ! ! ! differences detected on ./dbg.sh fs fuse !!!\n"
  cat $dif   
  return 1
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
  printf "! ! ! ! ! ! ! differences detected on ./dbg.sh $1 trx !!!\n"
  cat $dif   
  return 1
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
build_storage_failed_test_list () {
  for TST in $TST_STORAGE_FAILED
  do
    TSTS=`echo "$TSTS $TST/storageFailed"` 
  done
}
build_storage_reset_test_list () {
  for TST in $TST_STORAGE_RESET
  do
    TSTS=`echo "$TSTS $TST/storageReset"` 
  done  
}
build_storcli_reset_test_list () {
  for TST in $TST_STORCLI_RESET
  do
    TSTS=`echo "$TSTS $TST/StorcliReset"` 
  done    
}
build_all_test_list () {
  for TST in $TST_BASIC" "$TST_RW
  do
    TSTS=`echo "$TSTS $TST"` 
  done
  build_storage_failed_test_list
  build_storage_reset_test_list  
  build_storcli_reset_test_list
}
list_tests () {
  build_all_test_list
  idx=0
  for TST in $TSTS
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
run_test_list() {

  total_avant=`date +%s`

  FAIL=0
  SUSPECT=0
  printf "______________________________________________._________.____________.\n" >> $RESULT
  printf   "                 TEST NAME                    |  RESULT |  Duration  |\n" >> $RESULT
  printf "______________________________________________|_________|____________|\n" >> $RESULT

  for TST in $TSTS
  do
  
    test_number=$((test_number+1))
    
    printf "____________| %4d |__$TST\n" $test_number 

    result=0

    # Split the elementary test and the test conditions
    COND=`echo $TST | awk -F'/' '{print $2}'`
    FUNC=`echo $TST | awk -F'/' '{print $1}'`    
      
    # Save some rozodebug output before test    
    rozodebug_before
    avant=`date +%s`

    # Run the test according to the required conditions
    $COND $FUNC
    res_tst=$result
          
    # Check some rozodebug output after the test    
    apres=`date +%s`
    delay $((apres-avant))    
    rozodebug_after    
    res_dbg=$?
    
    
    
    if [ $res_tst != 0 ];
    then
      # The test returns an error code
      printf "!!!!!!!!!!!!!!!!! $TST is failed ($zedelay) !!!\n"
      printf "%3d %41s | FAILED  | %10s |\n" $test_number $TST "$zedelay" >> $RESULT
      FAIL=$((FAIL+1))
      if [ $nonstop == 0 ];
      then
        break
      fi	
    else          
      if [ $res_dbg != 0 ];
      then
        # The test do not complain but some debug ouput change need to be checked 
        SUSPECT=$((SUSPECT+1))
        printf "! ! ! ! ! ! ! $TST suspicion of failure ($zedelay) !\n"
	printf "%3d %41s | SUSPECT | %10s |\n" $test_number $TST "$zedelay" >> $RESULT
      else
        # The test is successfull
        printf "$TST success ($zedelay)\n"      
	printf "%3d %41s | OK      | %10s |\n" $test_number $TST "$zedelay" >> $RESULT  
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
  

}

name=`basename $0`
RESULT=/tmp/result
printf "\n" > $RESULT

# Default test dimensioning
fileSize=4
loop=64
process=8
NB_SID=8

# List of test
TST_RW="wr_rd_total wr_rd_partial wr_rd_random wr_rd_total_close wr_rd_partial_close wr_rd_random_close wr_close_rd_total wr_close_rd_partial wr_close_rd_random wr_close_rd_total_close wr_close_rd_partial_close wr_close_rd_random_close"
TST_STORAGE_FAILED="read_parallel $TST_RW"
TST_STORAGE_RESET="read_parallel $TST_RW"
TST_STORCLI_RESET="read_parallel $TST_RW"
TST_BASIC="readdir xattr link rename chmod truncate lock_posix_passing lock_posix_blocking read_parallel rw2"
# lock_bsd_passing lock_bsd_blocking

TSTS=""
repeated=1
nonstop=0
FAIL=0

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
    -repeated) repeated=$2;            shift 2;;
    -nonstop)  nonstop=1;              shift 1;;
    -sid)      NB_SID=$2;              shift 2;;
    # Debugging this tool
    -verbose|-v)  set -x;                 shift 1;;

    # help
    -*)        usage;;  

    # Read test list to execute
    all)                  build_all_test_list;               shift 1;;
    rw)                   TSTS=$TST_RW;                      shift 1;;
    storagedFailed)       build_storage_failed_test_list;    shift 1;;
    storagedReset)        build_storage_reset_test_list;     shift 1;;
    storcliReset)         build_storcli_reset_test_list;     shift 1;;
    basic)                TSTS=$TST_BASIC;                   shift 1;;
    *)                    TSTS=`echo "$TSTS $1"`;            shift 1;;
  esac  
done

# No test requested
if [ "$TSTS" == "" ];
then
  usage
fi

# Compile programs
compile_programs rw read_parallel test_xattr test_link test_write test_readdir test_rename test_chmod test_trunc test_file_lock rw2

# Kill export gateway
./setup.sh expgw all stop 

test_number=0

# execute the tests
while [ $repeated -ne 0 ];
do

  repeated=$((repeated-1))
  run_test_list 

  if [ $result != 0 ];
  then
    break
  fi 
   
done
cat $RESULT

if [ $FAIL != 0 ];
then
  exit 1
else
  exit 0
fi
