#!/bin/bash

ulimit -c unlimited

while [ 1 ];
do
  $* 
  sleep 1
done
