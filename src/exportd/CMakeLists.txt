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


add_definitions(-DD_REENTRANT -D_FILE_OFFSET_BITS=64)
include_directories(${CMAKE_BINARY_DIR}) # for config.h
include_directories(${CMAKE_SOURCE_DIR}) # for librozofs headers
include_directories(${PTHREAD_INCLUDE_DIR})
link_directories(${CMAKE_BINARY_DIR}/rozofs)

add_executable(exportd
    volume.h
    volume.c
    mreg.h
    mreg.c
    mdir.h
    mdir.c
    mslnk.h
    mslnk.c
#    # New mdirent version
    dirent_journal.h
    mdirent_vers2.h
    dirent_file_repair.c
    writeback_cache.c
    dirent_file.c
    dirent_cache.c
    dirent_search.c
    dirent_insert.c
    # Old mdirent version
    #mdirent.h
    #mdirent.c
    cache.h
    cache.c
    export.h
    export.c
    eproto.c
    monitor.h
    monitor.c
    econfig.h
    econfig.c
    epproto.c
    exportd.c
)
target_link_libraries(exportd rozofs ${PTHREAD_LIBRARY} ${UUID_LIBRARY} ${CONFIG_LIBRARY})


# Install
install(CODE "EXECUTE_PROCESS (COMMAND mkdir -p ${ROZOFS_CONFIG_DIR})" COMPONENT exportd)

install(TARGETS exportd
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    COMPONENT export
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/export.conf.sample DESTINATION ${ROZOFS_CONFIG_DIR} COMPONENT exportd)
