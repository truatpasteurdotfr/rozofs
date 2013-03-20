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

add_executable(storaged
    sproto.c
    mproto.c
    spproto.c
    sconfig.h
    sconfig.c
    storage.h
    storage.c
    rbs.h
    rbs_transform.h
    rbs_transform.c
    rbs_sclient.h
    rbs_sclient.c
    rbs_eclient.h
    rbs_eclient.c
    rbs.c
    storaged.h
    storaged.c
)
target_link_libraries(storaged rozofs ${PTHREAD_LIBRARY} ${UUID_LIBRARY} ${CONFIG_LIBRARY})


# Install
install(CODE "EXECUTE_PROCESS (COMMAND mkdir -p ${ROZOFS_CONFIG_DIR})" COMPONENT storage)

install(TARGETS storaged
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    COMPONENT storage
)

install(FILES storage.conf.sample DESTINATION ${ROZOFS_CONFIG_DIR} COMPONENT storage)
install(FILES storage.conf DESTINATION ${ROZOFS_CONFIG_DIR} COMPONENT storage)