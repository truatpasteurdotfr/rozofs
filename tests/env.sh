#!/bin/bash

#  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
#  This file is part of Rozofs.
#
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

NAME_LABEL="$(uname -a)"
DATE_LABEL="$(date +%d-%m-%Y-%H:%M:%S)"
WORKING_DIR=$PWD

# binaries
FDTREE_BINARY=${WORKING_DIR}/fs_ops/fdtree/fdtree.bash
FSOP_BINARY=${WORKING_DIR}/build/tests/fs_ops/fileop/fileop
IOZONE_BINARY=`which iozone`
RSYNC_BINARY=`which rsync`
BONNIE_BINARY=`which bonnie++`
GNUPLOT_BINARY=`which gnuplot`
VALGRIND_BINARY=`which valgrind`

# local env
LOCAL_SOURCE_DIR=$(dirname ${WORKING_DIR})
ROZOFS_SHELL_DIR=${WORKING_DIR}/../src/rozofsmount/
LOCAL_BUILD_DIR=${WORKING_DIR}/build
LOCAL_CMAKE_BUILD_TYPE=Release #Debug or Release
LOCAL_BINARY_DIR=${LOCAL_BUILD_DIR}/src
ROZOFS_BIN_DIR=${LOCAL_BINARY_DIR}/storcli
LOCAL_CONF=${WORKING_DIR}/config_files/
LOCAL_EXPORT_NAME_BASE=localhost
LOCAL_EXPORTS_NAME_PREFIX=export
LOCAL_EXPORTS_ROOT=${WORKING_DIR}/${LOCAL_EXPORTS_NAME_PREFIX}
LOCAL_EXPORT_DAEMON=exportd
LOCAL_EXPORT_CONF_FILE=export.conf
LOCAL_STORAGE_NAME_BASE=localhost
LOCAL_STORAGES_NAME_PREFIX=storage
LOCAL_STORAGES_ROOT=${WORKING_DIR}/${LOCAL_STORAGES_NAME_PREFIX}
LOCAL_STORAGE_DAEMON=storaged
LOCAL_STORAGE_REBUILD=storage_rebuild
LOCAL_STORAGE_CONF_FILE=storage.conf
LOCAL_MNT_PREFIX=mnt
LOCAL_MNT_ROOT=${WORKING_DIR}/${LOCAL_MNT_PREFIX}
LOCAL_ROZOFS_CLIENT=rozofsmount
LOCAL_ROZOFS_STORCLI=storcli
LOCAL_TEST_DIR=${LOCAL_BUILD_DIR}/tests/fs_ops
LOCAL_PJDTESTS=${LOCAL_TEST_DIR}/pjd-fstest
