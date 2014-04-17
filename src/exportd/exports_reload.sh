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

mountpoint=$1
instance=$2



reload_everybody ()
{
  count=0
  for pid in `ps -ef | grep "exportd -i $instance" | grep " $mountpoint" | grep -v "exports_starter.sh" | awk '{print $2}'`
  do
    kill -1 $pid
    count=$((count+1))
  done
  
  case $count in
    "0") exit;;
  esac
}


reload_everybody 
