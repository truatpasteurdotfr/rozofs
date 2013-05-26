#!/bin/sh

mountpoint=$1

kill_everybody ()
{
  for pid in `ps -ef | grep -v grep | grep "storio -i" | awk '{print $2}'`
  do
    kill $1 $pid
  done
}
kill_host ()
{
  for pid in `ps -ef | grep -v grep | grep "storio -i" | grep "H $h" | awk '{print $2}'`
  do
    kill $1 $pid
  done
}
kill_instance ()
{
  for pid in `ps -ef | grep -v grep | grep "storio -i $i" | grep "H $h" | awk '{print $2}'`
  do
    kill $1 $pid
  done
}

while [ ! -z $1 ];
do
  case "$1" in
    -d) set -x; shift 1;;
    -H) h=$2; shift 2;; 
    -i) i=$2; shift 2;;
    *) shift 1;;
  esac
done    

if [  ! -z $h  ] && [ ! -z $i ];
then
  kill_instance -6 
  kill_instance -9
  exit
fi

if [ ! -z $h ];
then
  kill_host -6 
  kill_host -9
  exit
fi

kill_everybody -6
kill_everybody -9
