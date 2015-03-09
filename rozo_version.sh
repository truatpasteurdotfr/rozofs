#!/bin/bash

ROZO_DATE=`date "+%y.%m.%d.%H.%M" | tr -d '\n' `
ROZO_BRANCH=`git rev-parse --abbrev-ref HEAD | sed 's#/#.#'`
ROZO_COMMIT=`git rev-parse HEAD`
printf "%s.%s.%s"  ${ROZO_MAJOR_MINOR} ${ROZO_DATE} ${ROZO_BRANCH} ${ROZO_COMMIT} 
