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

[[ -z ${IOZONE_BINARY} ]] && echo "Can't find iozone." && exit -1

#check if mountable
#mountable="`grep -cE ^rozofsmount.*$1.*exporthost=$2,exportpath=$3 /etc/fstab`"
#if [[ $mountable == "0" ]]
#then
#    echo "$1 is not mountable"
#    echo "add: line below in your /etc/fstab" 
#    echo "rozofsmount     `readlink -f $1`     rozofs  exporthost=$2,exportpath=$3,_netdev  0       0"
#    exit 0
#fi


iozone -i 0 -i 2 -i 3 -f ${1}test_random_ -s 10m -r 8k


exit 0
