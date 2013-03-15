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

#set -x
HARDDIR=hard
SYMDIR=sym

test_regular_file() {

  if [ ! -e $1 ];
  then
    echo "[$count] [$cmd]  $1 is not created !!!"
    exit
  fi 
  
  if [ ! -f $1 ];
  then
    echo "[$count] [$cmd]  $1 is not a regular file !!!"
    exit
  fi 
  
}
test_symlink_file() {

  if [ ! -e $1 ];
  then
    echo "[$count] [$cmd]  $1 is not created !!!"
    exit
  fi 
  
  if [ ! -h $1 ];
  then
    echo "[$count] [$cmd]  $1 is not a symbolic link file !!!"
    exit
  fi 
  
}
test_no_file() {

  if [ -e $1 ];
  then
    echo "[$count] [$cmd]  $1 exist !!!"
    exit
  fi 
}
test_files() {

  if [ $is_reg -eq 1 ];
  then
    test_regular_file $REG 
  else
    test_no_file $REG
  fi
  
  if [ $is_hard -eq 1 ];
  then
    test_regular_file $HARD
  else
    test_no_file $HARD
  fi

  if [ $is_sym -eq 1 ];
  then
    test_symlink_file $SYM
  else
    test_no_file $SYM
  fi  
}
new_loop() {
  count=`expr $count + 1`
  if [ `expr $count % 100` -eq `expr 0` ];
  then
    echo "Starting loop $count" 
  fi
}
usage () {
  echo "$0: <mount point>"
  exit
}
do_cmd() {
  cmd="$1"
  $cmd
  test_files
}



[[ $# -lt 1 ]] && usage

SRC=`pwd`/CMakeLists.txt
DIR=`pwd`/$1
REG=$DIR/f_regular
SYM=$DIR/$SYMDIR/f_symlink
HARD=$DIR/$HARDDIR/f_hardlink


rm $REG
rm $SYM
rm $HARD

count=0
cmd="start up"
is_reg=0
is_sym=0
is_hard=0
test_files


while [ 1 ];
do

  new_loop

  cd $DIR
  mkdir -p $HARDDIR
  mkdir -p $SYMDIR

  is_reg=1; do_cmd "cp $SRC $REG"
  is_sym=1; do_cmd "ln -s $REG $SYM"  
  is_hard=1;do_cmd  "ln $REG $HARD"; 
  is_sym=0; do_cmd "unlink $SYM"
  is_reg=0; do_cmd "unlink $REG"
  is_hard=0; do_cmd "unlink $HARD"

done
