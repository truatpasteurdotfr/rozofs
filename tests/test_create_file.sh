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
DEFAULT_FILES=500
DEFAULT_PROCESSES=5
usage () {
  echo
  echo "test_create_file.sh [-n <nb>] [<path>]\n"
  echo "-n <nb>       to give the number of files to create in the directory to read."
  echo "              default is $DEFAULT_FILES"
  echo "-t <times>     Number of times to do the <nb> files creation (default 1)"
  echo "<path>        where to create the files (default is .)"  
  exit
}

FILE_NB=$DEFAULT_FILES
DIR="."
times=1

[[ $# -lt 1 ]] && usage

while [ ! -z $1 ];
do
  case $1 in
    "-n") {
      if [ -z $2 ]
      then
        echo "Missing file number after $1 !!!"
	usage
      fi
      FILE_NB=$2
      shift 2
    };;
    
    "-t") {
      if [ -z $2 ]
      then
        echo "Missing times number after $1 !!!"
	usage
      fi
      times=$2
      shift 2
    };;    
    
    *) {
      if [ -z $2 ];
      then
        DIR=$1
	shift 1
      else
        echo "Too much arguments"
	usage	
      fi      
    };;
  esac
done 


if [ ! -d $DIR ];
then
  echo "$DIR is not a directory !!!"
  usage
fi
cd $DIR


for loop in $(seq 1 1 $times);
do

  DATE=`date +'%H_%M_%S'`

  # Create files
  for idx in $(seq 1 1 $FILE_NB);
  do
    name=`printf "File_%4.4u.%s" $idx $DATE` 
    echo "$name" > ./$name
  done
  echo "$FILE_NB files with date $DATE created under $DIR"
done
