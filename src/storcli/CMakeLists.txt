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

add_executable(storcli
    rozofs_storcli.h
    rozofs_storcli_transform.h
    rozofs_storcli_lbg_cnf_supervision.h
    storage_proto.h
    rozofs_storcli_rpc.h        
    storcli_main.h
    rozofs_storcli_send_new.c
    rozofs_storcli_lbg_cnf_supervision.c
    rozofs_storcli_transform_utility.c
    rozofs_storcli_transform_128_opt.c
    rozofs_storcli_mgt.c
    rozofs_storcli_write.c
    rozofs_storcli_nblock_init.c
    storcli_main.c
    rozofs_storcli_north_intf.c
    storcli_stub.c
    rozofs_storcli_read.c
    rozofs_storcli_rebuild_prj.c
    rozofs_storcli_reload_storage_config.c
    rozofs_storcli_reload_storage_config.h
    storage_lbg.c
    rozofs_storcli_cnx_supervision.c
    rozofs_storcli_truncate.c        
    storcli_ring.c        
    storcli_ring.h        
    rozofs_storcli_mojette_thread.c
    rozofs_storcli_mojette_thread_intf.c 
    rozofs_storcli_mojette_thread_intf.h
    rozofs_storcli_delete.c
    rozofs_storcli_wr_repair.c
    rozofs_storcli_crypt.c
# Mojette OPT
 ./moj128_opt/0_transform_layout0_blksz4096.c
 ./moj128_opt/0_transform_layout0_blksz8192.c
 ./moj128_opt/0_transform_layout1_blksz4096.c
 ./moj128_opt/0_transform_layout1_blksz8192.c
 ./moj128_opt/10_transform_layout1_blksz4096.c
 ./moj128_opt/10_transform_layout1_blksz8192.c
 ./moj128_opt/11_transform_layout1_blksz4096.c
 ./moj128_opt/11_transform_layout1_blksz8192.c
 ./moj128_opt/12_transform_layout1_blksz4096.c
 ./moj128_opt/12_transform_layout1_blksz8192.c
 ./moj128_opt/13_transform_layout1_blksz4096.c
 ./moj128_opt/13_transform_layout1_blksz8192.c
 ./moj128_opt/14_transform_layout1_blksz4096.c
 ./moj128_opt/14_transform_layout1_blksz8192.c
 ./moj128_opt/1_transform_layout0_blksz4096.c
 ./moj128_opt/1_transform_layout0_blksz8192.c
 ./moj128_opt/1_transform_layout1_blksz4096.c
 ./moj128_opt/1_transform_layout1_blksz8192.c
 ./moj128_opt/2_transform_layout0_blksz4096.c
 ./moj128_opt/2_transform_layout0_blksz8192.c
 ./moj128_opt/2_transform_layout1_blksz4096.c
 ./moj128_opt/2_transform_layout1_blksz8192.c
 ./moj128_opt/3_transform_layout1_blksz4096.c
 ./moj128_opt/3_transform_layout1_blksz8192.c
 ./moj128_opt/4_transform_layout1_blksz4096.c
 ./moj128_opt/4_transform_layout1_blksz8192.c
 ./moj128_opt/5_transform_layout1_blksz4096.c
 ./moj128_opt/5_transform_layout1_blksz8192.c
 ./moj128_opt/6_transform_layout1_blksz4096.c
 ./moj128_opt/6_transform_layout1_blksz8192.c
 ./moj128_opt/7_transform_layout1_blksz4096.c
 ./moj128_opt/7_transform_layout1_blksz8192.c
 ./moj128_opt/8_transform_layout1_blksz4096.c
 ./moj128_opt/8_transform_layout1_blksz8192.c
 ./moj128_opt/9_transform_layout1_blksz4096.c
 ./moj128_opt/9_transform_layout1_blksz8192.c
 ./moj128_opt/mojette_inverse_api_opt.c
 ./moj128_opt/transform_api.h
 ./moj128_opt/transform_default_layout0.c
 ./moj128_opt/transform_default_layout1.c
 ./moj128_opt/transform_default_layout2.c
 ./moj128_opt/transform_fwd_layout0_blksz1024.c
 ./moj128_opt/transform_fwd_layout1_blksz1024.c
 ./moj128_opt/transform_fwd_layout2_blksz1024.c
 ./moj128_opt/transform_layout0_blksz4096.c
 ./moj128_opt/transform_layout0_blksz4096.h
 ./moj128_opt/transform_layout0_blksz8192.c
 ./moj128_opt/transform_layout0_blksz8192.h
 ./moj128_opt/transform_layout1_blksz4096.c
 ./moj128_opt/transform_layout1_blksz4096.h
 ./moj128_opt/transform_layout1_blksz8192.c
 ./moj128_opt/transform_layout1_blksz8192.h
 ./moj128_opt/transform_module.h
# sse section
 ./moj128_opt/transform128_fwd_layout0_blksz2048_sse.c
 ./moj128_opt/transform128_fwd_layout0_blksz4096_sse.c
 ./moj128_opt/transform128_fwd_layout0_blksz8192_sse.c
 ./moj128_opt/transform128_fwd_layout1_blksz2048_sse.c
 ./moj128_opt/transform128_fwd_layout1_blksz4096_sse.c
 ./moj128_opt/transform128_fwd_layout1_blksz8192_sse.c
 ./moj128_opt/transform128_fwd_layout2_blksz2048_sse.c
 ./moj128_opt/transform128_fwd_layout2_blksz4096_sse.c
 ./moj128_opt/transform128_fwd_layout2_blksz8192_sse.c

)
#pthread need by fuse
target_link_libraries(storcli rozofs ${PTHREAD_LIBRARY} ${CRYPT_LIBRARY} ${AES_LIBRARY})

# Install
install(TARGETS storcli
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    COMPONENT storcli
)
