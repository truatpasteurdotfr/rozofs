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

add_executable(geocli
    file.h
    rozofsmount.c
    rozofs_tx_client_export.c
    rozofs_non_blocking_main.c
    rozofsmount.h
    rozofs_sharedmem.c
    rozofs_sharedmem.h
    rozofs_rw_load_balancing.h
    rozofs_rw_load_balancing.c
    geocli_lbg.c
    geocli_srv.c
    geocli_srv.h
    rzcp_file_ctx.c
    rzcp_file_ctx.h
    rzcp_copy.c
    rzcp_copy.h
    rzcp_read.c
    rzcp_write.c
    rzcp_remove.c
    geocli_file_copy.c
)
add_executable(geomgr
    geomgr.c
    geomgr.h
    geomgr_config.c
    geomgr_config.h    
)

#pthread need by fuse
target_link_libraries(geocli rozofs ${PTHREAD_LIBRARY} ${CRYPT_LIBRARY})
target_link_libraries(geomgr rozofs ${PTHREAD_LIBRARY} ${CONFIG_LIBRARY} )

# Install
install(TARGETS geocli
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    COMPONENT geomgr
)
install(TARGETS geomgr
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    COMPONENT geomgr
)
