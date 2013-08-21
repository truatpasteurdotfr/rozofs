#!/bin/bash

kill_everybody ()
{
  nb=0
  for pid in `ps -ef | grep -v grep | grep "storio -i" | awk '{print $2}'`
  do
    kill $1 $pid
    nb=$((nb+1))
  done
  case $nb in
    "0") exit;;
  esac   
}
kill_host ()
{
  nb=0
  for pid in `ps -ef | grep -v grep | grep "storio -i" | grep "H $h" | awk '{print $2}'`
  do
    kill $1 $pid
    nb=$((nb+1))
  done
  case $nb in
    "0") exit;;
  esac    
}
kill_instance ()
{
  nb=0
  for pid in `ps -ef | grep -v grep | grep "storio -i $i" | grep "H $h" | awk '{print $2}'`
  do
    kill $1 $pid
    nb=$((nb+1))
  done
  case $nb in
    "0") exit;;
  esac  
}

while [ ! -z $1 ];
do
  case "$1" in
    -v) set -x; shift 1;;
    -H) h=$2; shift 2;; 
    -i) i=$2; shift 2;;
    *) shift 1;;
  esac
done    

if [  ! -z $h  ] && [ ! -z $i ];
then
  kill_instance 
  sleep 2
  kill_instance -9
  exit
fi

if [ ! -z $h ];
then
  kill_host 
  sleep 2
  kill_host -9
  exit
fi

kill_everybody 
sleep 2
kill_everybody -9
