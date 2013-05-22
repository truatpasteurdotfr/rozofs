#!/bin/sh

mountpoint=$1

kill_everybody ()
{
  for pid in `ps -ef | grep "storcli "| grep "M $mountpoint" | awk '{print $2}'`
  do
    kill $1 $pid
  done
}

kill_everybody -6
kill_everybody -9
