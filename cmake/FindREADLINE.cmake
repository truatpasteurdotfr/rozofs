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

# - Find pthread
# Find the native PTHREAD includes and library
#
#  READLINE_INCLUDE_DIR - where to find pthread.h, etc.
#  READLINE_LIBRARIES   - List of libraries when using pthread.
#  READLINE_FOUND       - True if pthread found.


FIND_PATH(READLINE_INCLUDE_DIR readline.h
  /usr/local/include/readline
  /usr/local/include
  /usr/include/readline
  /usr/include
)

SET(READLINE_NAMES readline)
FIND_LIBRARY(READLINE_LIBRARY
  NAMES ${READLINE_NAMES}
  PATHS /usr/lib /usr/local/lib
)

IF(READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
  SET(READLINE_FOUND TRUE)
  SET(READLINE_LIBRARIES ${READLINE_LIBRARY} )
ELSE(READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
  SET(READLINE_FOUND FALSE)
  SET(READLINE_LIBRARIES)
ENDIF(READLINE_INCLUDE_DIR AND READLINE_LIBRARY)

IF(NOT READLINE_FOUND)
   IF(READLINE_FIND_REQUIRED)
     MESSAGE(FATAL_ERROR "readline library and headers required.")
   ENDIF(READLINE_FIND_REQUIRED)
ENDIF(NOT READLINE_FOUND)

MARK_AS_ADVANCED(
  READLINE_LIBRARY
  READLINE_INCLUDE_DIR
)
