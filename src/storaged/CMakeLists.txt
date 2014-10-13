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
    mproto.c
    mprotosvc.h
    spproto.c
    sconfig.h
    sconfig.c
    storage.h
    storage.c
    storaged.h
    storaged.c
    storaged_nblock_init.h
    storaged_nblock_init.c
    storaged_north_intf.c
    storaged_north_intf.h
)
target_link_libraries(storaged rozofs ${PTHREAD_LIBRARY} ${UUID_LIBRARY} ${CONFIG_LIBRARY})

add_executable(storage_rebuild
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
    storage_rebuild.c
)
target_link_libraries(storage_rebuild rozofs ${PTHREAD_LIBRARY} ${UUID_LIBRARY} ${CONFIG_LIBRARY})

add_executable(storage_list_rebuilder
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
    storage_list_rebuilder.c       
)
target_link_libraries(storage_list_rebuilder rozofs ${PTHREAD_LIBRARY} ${UUID_LIBRARY} ${CONFIG_LIBRARY})

add_executable(storio_reload
    storio_reload.c
    storage.h
)
target_link_libraries(storio_reload )


add_executable(storio
    spproto.c
    sconfig.h
    sconfig.c
    storage.h
    storage.c
    sproto_nb.h
    sproto_nb.c
    sprotosvc_nb.c
    sprotosvc_nb.h
    storio_nblock_init.h
    storio_nblock_init.c
    storio_north_intf.c
    storio_north_intf.h
    storage_fd_cache.c
    storage_fd_cache.h
    storio.c
    storio_cache.h
    storio_cache.c
    storio_bufcache.c
    storio_bufcache.h
    storio_disk_thread_intf.h
    storio_disk_thread_intf.c
    storio_disk_thread.c
    storio_device_mapping.c
    storio_device_mapping.h
    storio_serialization.h
    storio_serialization.c
)
target_link_libraries(storio rozofs ${PTHREAD_LIBRARY} ${UUID_LIBRARY} ${CONFIG_LIBRARY})

# Install
install(CODE "EXECUTE_PROCESS (COMMAND mkdir -p ${ROZOFS_CONFIG_DIR})" COMPONENT storage)

install(TARGETS storaged
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    COMPONENT storage
)
install(TARGETS storage_rebuild
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    COMPONENT storage_rebuild
)
install(TARGETS storage_list_rebuilder
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    COMPONENT storage_list_rebuilder
)        
install(TARGETS storio_reload
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    COMPONENT storio_reload
)
install(TARGETS storio
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    COMPONENT storio
)
install(FILES storage.conf.sample DESTINATION ${ROZOFS_CONFIG_DIR} COMPONENT storage)
install(FILES storage.conf DESTINATION ${ROZOFS_CONFIG_DIR} COMPONENT storage)
