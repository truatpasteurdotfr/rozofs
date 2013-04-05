#!/bin/sh

mountpoint=$1

kill_everybody ()
{
  for pid in `ps -ef | grep storcli | grep $mountpoint | awk '{print $2}'`
  do
    kill -9 $pid
  done
}

kill_everybody
kill_everybody
