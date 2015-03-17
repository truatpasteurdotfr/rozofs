#!/bin/bash

if [ -d "$1" ];
then 
  cd $1
fi

ROZO_DATE=`date "+%Y/%m/%d %HH%M" | tr -d '\n' `
ROZO_BRANCH=`git rev-parse --abbrev-ref HEAD `
ROZO_COMMIT=`git rev-parse HEAD`
case `grep "./moj128_opt/" $1/src/storcli/CMakeLists.txt` in
  "") ROZO_MOJ="LEGACY";;
  *)  ROZO_MOJ="FSX";;
esac   
printf "${ROZO_DATE} ${ROZO_BRANCH} ${ROZO_COMMIT} ${ROZO_MOJ}" 
