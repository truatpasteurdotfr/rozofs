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

for proc in  `ps -ef | grep "$1" | grep -v grep | grep -v killer | awk '{print $2}'`
do
  case "$proc" in
    0);;
    *) kill $proc >/dev/null 2>/dev/null ;;
  esac  
done

sleep 0.1

for proc in  `ps -ef | grep "$1" | grep -v grep | grep -v killer | awk '{print $2}'`
do
  case "$proc" in
    0);;
    *) kill -9 $proc >/dev/null 2>/dev/null ;;
  esac  
done
