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

# - Find numa
# Find the native NUMA includes and library
#
#  NUMA_INCLUDE_DIR - where to find numa.h, etc.
#  NUMA_LIBRARIES   - List of libraries when using numa.
#  NUMA_FOUND       - True if fuse found.

FIND_PATH(NUMA_INCLUDE_DIR numa.h
  /usr/include
)

SET(NUMA_NAMES numa)
FIND_LIBRARY(NUMA_LIBRARY
  NAMES ${NUMA_NAMES}
  PATHS /usr/lib /usr/local/lib
)

IF(NUMA_INCLUDE_DIR AND NUMA_LIBRARY)
  SET(NUMA_FOUND TRUE)
  SET(NUMA_LIBRARIES ${NUMA_LIBRARY} )
ELSE(NUMA_INCLUDE_DIR AND NUMA_LIBRARY)
  SET(NUMA_FOUND FALSE)
  SET(NUMA_LIBRARIES)
ENDIF(NUMA_INCLUDE_DIR AND NUMA_LIBRARY)

IF(NOT NUMA_FOUND)
   IF(NUMA_FIND_REQUIRED)
     MESSAGE(FATAL_ERROR "numa library and headers required.")
   ENDIF(NUMA_FIND_REQUIRED)
ENDIF(NOT NUMA_FOUND)

MARK_AS_ADVANCED(
  NUMA_LIBRARY
  NUMA_INCLUDE_DIR
)
