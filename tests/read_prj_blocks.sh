#!/bin/bash
#set -x
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


attr -g rozofs $1 > $TMP
LAYOUT=`awk '{ if ($1=="LAYOUT") print $3; }' $TMP`
case $LAYOUT in
  "0") HOSTNB=4;;
  "1") HOSTNB=8;;
  "2") HOSTNB=16;;
esac  
VID=`awk '{ if ($1=="VID") print $3; }' $TMP`
BSIZE=`awk '{ if ($1=="BSIZE") print $3; }' $TMP` 
FID=`awk '{ if ($1=="FID") print $3; }' $TMP` 
cid=`awk '{ if ($1=="CLUSTER") print $3; }' $TMP`
DIST=`awk '{ if ($1=="STORAGE") print $3; }' $TMP`
SIZE=`awk '{ if ($1=="SIZE") print $3; }' $TMP`
MAX_SIZE=`awk '{if (($1=="#define")&&($2=="ROZOFS_STORAGE_FILE_MAX_SIZE")) printf("%s\n",$3);}' ../rozofs/rozofs.h | awk -F"UL" '{ print $1$2; }'`
CHUNK_SIZE=$((MAX_SIZE/128))
chunks=$((SIZE/CHUNK_SIZE)) 

bsize=4096
case $BSIZE in
  0) ;;
  1) bsize=$((bsize*2));;
  2) bsize=$((bsize*4));;
  2) bsize=$((bsize*8));;
esac

zeblock=$2
case "$zeblock" in
  "")oneblock="";;
  *) oneblock="-n $zeblock"; onechunk=$(( (zeblock*bsize)/CHUNK_SIZE ));
esac  


sids=""
for val in `echo $DIST | awk -F'-' '{ print $1" "$2" "$3" "$4" "$5" "$6" "$7" "$8; }'`
do
  cent=${val:0:1}
  dix=${val:1:1}
  unit=${val:2:1}
  
  sid=$((cent*100+dix*10+unit))
  sids="$sids $sid"
done

echo "$FID cid $cid sids $sids bsize $BSIZE" 
for chunk in $(seq 0 $chunks)
do

  case "$oneblock" in
    "") ;;
    *) {
      if [ $chunk -ne $onechunk ];
      then
        continue
      fi
    };;
  esac

  list=""
  empty=1
  devices=`printf "%16s" " "`
  for sid in `echo $sids`
  do

    name=`printf "%s-%3.3d" $FID $chunk`
    locsid=$(( (sid-1)%HOSTNB + 1 ))
    res=`find config_file/storage_*_${cid}_${sid} -name "$name"`
    case $res in 
      "") {
        list="$list -f NULL"
	devices=`printf "%s %31s" "$devices" "$cid/$sid[-]"`
#	devices=`"$devices $cid/$sid[-]"
      };;
      *)  {
        list="$list -f $res"
	empty=0
	dev=`echo $res | awk -F'/' '{print $2}'`
	devices=`printf "%s %31s" "$devices" "$cid/$sid[$dev]"`	
#	devices="$devices $cid/$sid[$dev]"
      };;
    esac
  done  
  case "$empty" in
    "0") {
       echo "________________FID $FID chunk #$chunk"
       echo "$devices"
       echo "./build/tests/read_prj_blocks $list -b $BSIZE -l $LAYOUT -c $chunk $oneblock"
       ./build/tests/read_prj_blocks $list -b $BSIZE -l $LAYOUT -c $chunk $oneblock
    };;
  esac  
done
