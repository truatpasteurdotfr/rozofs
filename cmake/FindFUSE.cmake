# Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
# This file is part of Rozofs.
#
# Rozofs is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published
# by the Free Software Foundation, version 2.
#
# Rozofs is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see
# <http://www.gnu.org/licenses/>.

# - Find fuse
# Find the native FUSE includes and library
#
#  FUSE_INCLUDE_DIR - where to find fuse.h, etc.
#  FUSE_LIBRARIES   - List of libraries when using fuse.
#  FUSE_FOUND       - True if fuse found.
#  FUSE_MAJOR_VERSION       - The major as define in fuse_common.h 
#  FUSE_MINOR_VERSION       - The minor as define in fuse_common.h 

FIND_PATH(FUSE_INCLUDE_DIR fuse.h
  /usr/local/include/fuse
  /usr/local/include
  /usr/include/fuse
  /usr/include
)

SET(FUSE_NAMES fuse)
FIND_LIBRARY(FUSE_LIBRARY
  NAMES ${FUSE_NAMES}
  PATHS /usr/lib /usr/local/lib
)

IF(FUSE_INCLUDE_DIR)
    file(STRINGS "${FUSE_INCLUDE_DIR}/fuse/fuse_common.h" line REGEX ".*#define FUSE_MAJOR_VERSION *([0-9]+)")
    string(REGEX REPLACE ".*# *define *FUSE_MAJOR_VERSION *([0-9]+).*" "\\1" FUSE_MAJOR_VERSION "${line}")
    file(STRINGS "${FUSE_INCLUDE_DIR}/fuse/fuse_common.h" line REGEX ".*#define FUSE_MINOR_VERSION *([0-9]+)")
    string(REGEX REPLACE ".*# *define *FUSE_MINOR_VERSION *([0-9]+).*" "\\1" FUSE_MINOR_VERSION "${line}")
    math(EXPR FUSE_VERSION "10 * ${FUSE_MAJOR_VERSION} + ${FUSE_MINOR_VERSION}")
    if (${FUSE_VERSION} LESS 29)
        message (FATAL_ERROR "Fuse 2.9 or greater required (${FUSE_MAJOR_VERSION}.${FUSE_MINOR_VERSION} found).")
    endif (${FUSE_VERSION} LESS 29)
ENDIF(FUSE_INCLUDE_DIR)


IF(FUSE_INCLUDE_DIR AND FUSE_LIBRARY)
  SET(FUSE_FOUND TRUE)
  SET(FUSE_LIBRARIES ${FUSE_LIBRARY} )
ELSE(FUSE_INCLUDE_DIR AND FUSE_LIBRARY)
  SET(FUSE_FOUND FALSE)
  SET(FUSE_LIBRARIES)
ENDIF(FUSE_INCLUDE_DIR AND FUSE_LIBRARY)

IF(NOT FUSE_FOUND)
   IF(FUSE_FIND_REQUIRED)
     MESSAGE(FATAL_ERROR "fuse library and headers required.")
   ENDIF(FUSE_FIND_REQUIRED)
ENDIF(NOT FUSE_FOUND)

MARK_AS_ADVANCED(
  FUSE_LIBRARY
  FUSE_INCLUDE_DIR
)