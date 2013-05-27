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

# - Find ncurses
# Find the native NCURSES includes and library
#
#  NCURSES_INCLUDE_DIR - where to find ncurses.h, etc.
#  NCURSES_LIBRARIES   - List of libraries when using ncurses.
#  NCURSES_FOUND       - True if ncurses found.


FIND_PATH(NCURSES_INCLUDE_DIR ncurses.h
  /usr/local/include
  /usr/include
)

SET(NCURSES_NAMES ncurses)
FIND_LIBRARY(NCURSES_LIBRARY
  NAMES ${NCURSES_NAMES}
  PATHS /usr/lib /usr/local/lib
)

IF(NCURSES_INCLUDE_DIR AND NCURSES_LIBRARY)
  SET(NCURSES_FOUND TRUE)
  SET(NCURSES_LIBRARIES ${NCURSES_LIBRARY} )
ELSE(NCURSES_INCLUDE_DIR AND NCURSES_LIBRARY)
  SET(NCURSES_FOUND FALSE)
  SET(NCURSES_LIBRARIES)
ENDIF(NCURSES_INCLUDE_DIR AND NCURSES_LIBRARY)

IF(NOT NCURSES_FOUND)
   IF(NCURSES_FIND_REQUIRED)
     MESSAGE(FATAL_ERROR "ncurses library and headers required.")
   ENDIF(NCURSES_FIND_REQUIRED)
ENDIF(NOT NCURSES_FOUND)

MARK_AS_ADVANCED(
  NCURSES_LIBRARY
  NCURSES_INCLUDE_DIR
)
