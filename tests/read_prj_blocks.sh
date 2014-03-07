#!/bin/bash

TMP="/tmp/.read_prj_blocks"

usage() {
  echo "$1"
  echo
  echo "read_prj_blocks <filename> [<block#>]"
  exit
}   


case "$1" in
  "") usage "Missing file name";;
esac

./setup.sh cou $1 > $TMP
 
list="" 
for f in `grep bins $TMP | awk '{ print $2 }'`
do
  list=`echo $list " -f $f"`
done  

case "$2" in
  "");;
  *) list=`echo $list " -b $2"`;;
esac
  
./build/tests/read_prj_blocks $list
