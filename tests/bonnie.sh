#!/bin/bash

#  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
#  This file is part of Rozofs.
#  Rozofs is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published
#  by the Free Software Foundation, version 2.
#  Rozofs is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see
#  <http://www.gnu.org/licenses/>.

. env.sh

usage() {
	echo "$0: <mount point>"
	exit 0
}

[[ $# -lt 1 ]] && usage

[[ -z ${BONNIE_BINARY} ]] && echo "Can't find bonnie++." && exit -1

flog=${WORKING_DIR}/bonnie++_`date "+%Y%m%d_%Hh%Mm%Ss"`_`basename $1`.log
#${BONNIE_BINARY} -d $1 -n 200 -m testedhost -s 16384 -f -u nobody 2>&1 | tee $flog


# -d: the directory to use for the tests
# -n: the number of files for the file creation test. This is measured in multiples of 1024 files
# -s: the size of the file(s) for IO performance measures in megabytes.
# -f: fast mode control, skips per-char IO tests if no parameter, otherwise specifies the size of the tests for per-char IO tests (default 20M).
# -u: user-id to use.


${BONNIE_BINARY} -d $1 -n 5 -s 8192 -f -u root:root  2>&1 | tee $flog

exit 0
