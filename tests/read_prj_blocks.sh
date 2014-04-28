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
BSIZE=`awk '{ if ($1=="BSIZE") print $3; }' $TMP` 
FID=`awk '{ if ($1=="FID") print $3; }' $TMP` 
DIST=`awk '{ if ($1=="STORAGE") print $3; }' $TMP`
DIST=`echo $DIST | awk -F'-' '{ print $1" "$2" "$3" "$4" "$5" "$6" "$7" "$8; }'`

echo "bsize $BSIZE" 
for sid in `echo $DIST`
do
  sid=$((sid+0)) 
  cid=$((sid-1))
  cid=$((cid/4))
  cid=$((cid+1))
  
  
  res=`grep storage_$cid-$sid $TMP | grep bins | awk '{ print $2; }'`
  case $res in 
    "") list="$list -f NULL"; echo " - NULL";;
    *)  list="$list -f $res"; echo " - $res";;
  esac  
done  

case "$2" in
  "");;
  *) list="$list -n $2";;
esac
  
./build/tests/read_prj_blocks $list -b $BSIZE
