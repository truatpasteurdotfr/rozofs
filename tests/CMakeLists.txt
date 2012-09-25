include_directories("${CMAKE_SOURCE_DIR}/src")
include_directories(${CMAKE_BINARY_DIR}/src) # for config.h

add_executable(test_list
    ../src/list.h
    test_list.c
)

add_executable(test_htable
    ../src/xmalloc.h
    ../src/xmalloc.c
    ../src/htable.h
    ../src/htable.c
    test_htable.c
)

add_executable(test_dist
    ../src/xmalloc.h
    ../src/xmalloc.c
    ../src/dist.h
    test_dist.c
)

add_executable(test_transform
    ../src/xmalloc.h
    ../src/xmalloc.c
    ../src/transform.h
    ../src/transform.c
    test_transform.c
)

add_executable(rpc_throughput
    ../src/rpcclt.h
    ../src/rpcclt.c
    rpc_throughput.h
    rpc_throughputxdr.c
    rpc_throughputclt.c
    rpc_throughput_client.c
)

add_executable(rpc_throughput_server
    rpc_throughput.h
    rpc_throughputxdr.c
    rpc_throughput_server.c
    rpc_throughputsvc.c
)

add_executable(transform_throughput
    ../src/xmalloc.h
    ../src/xmalloc.c
    ../src/transform.h
    ../src/transform.c
    transform_throughput.c
)

add_executable(transform_file
    ../src/xmalloc.h
    ../src/xmalloc.c
    ../src/transform.h
    ../src/transform.c
    transform_file.c
)

add_executable(test_volume
    ../src/xmalloc.h
    ../src/xmalloc.c
    ../src/rozofs.h
    ../src/rozofs.c
    ../src/rpcclt.h
    ../src/rpcclt.c
    ../src/sproto.h
    ../src/sprotoxdr.c
    ../src/sprotoclt.c
    ../src/storageclt.c
    ../src/volume.h
    ../src/volume.c
    test_volume.c
)
target_link_libraries(test_volume ${PTHREAD_LIBRARY})

add_executable(test_storage
    ../src/xmalloc.h
    ../src/xmalloc.c
    ../src/rozofs.h
    ../src/rozofs.c
    ../src/htable.h
    ../src/htable.c
    ../src/storage.h
    ../src/storage.c
    test_storage.c
)
target_link_libraries(test_storage ${UUID_LIBRARY})

add_executable(test_sconfig
    ../src/xmalloc.h
    ../src/xmalloc.c
    ../src/sconfig.h
    ../src/sconfig.c
    test_sconfig.c
)
target_link_libraries(test_sconfig ${CONFIG_LIBRARY})

add_executable(test_econfig
    ../src/xmalloc.h
    ../src/xmalloc.c
    ../src/econfig.h
    ../src/econfig.c
    test_econfig.c
)
target_link_libraries(test_econfig ${CONFIG_LIBRARY})

add_executable(test_mdirent_vers2
    ../src/xmalloc.h
    ../src/xmalloc.c
    ../src/dirent_journal.h
    ../src/mdirent_vers2.h
    ../src/dirent_file_repair.c
    ../src/writeback_cache.c
    ../src/dirent_file.c
    ../src/dirent_cache.c
    ../src/dirent_search.c
    ../src/dirent_insert.c
    test_mdirent_vers2.c
)
target_link_libraries(test_mdirent_vers2 ${UUID_LIBRARY})

add_executable(test_mdirent_vers1
    ../src/xmalloc.h
    ../src/xmalloc.c
    ../src/mdirent_vers1.h
    ../src/mdirent_vers1.c
    test_mdirent_vers1.c
)
target_link_libraries(test_mdirent_vers1 ${UUID_LIBRARY})

add_executable(test_export
    ../src/xmalloc.h
    ../src/xmalloc.c
    ../src/htable.h
    ../src/htable.c
    ../src/list.h
    ../src/rozofs.h
    ../src/rozofs.c
    ../src/rpcclt.h
    ../src/rpcclt.c
    ../src/sproto.h
    ../src/sprotoxdr.c
    ../src/sprotoclt.c
    ../src/storageclt.c
    ../src/volume.h
    ../src/volume.c
    ../src/volume.h
    ../src/volume.c
    ../src/mattr.h
    ../src/mattr.c
    ../src/cache.h
    ../src/cache.c
    ../src/mreg.h
    ../src/mreg.c
    ../src/mdir.h
    ../src/mslnk.c
    ../src/mslnk.h
    ../src/mdir.c
    # New mdirent version
    ../src/dirent_journal.h
    ../src/mdirent_vers1.h
    ../src/dirent_file_repair.c
    ../src/writeback_cache.c
    ../src/dirent_file.c
    ../src/dirent_cache.c
    ../src/dirent_search.c
    ../src/dirent_insert.c
    # Old mdirent version
    #../src/mdirent.h
    #../src/mdirent.c
    ../src/export.h
    ../src/export.c
    test_export.c
)
target_link_libraries(test_export ${UUID_LIBRARY} ${PTHREAD_LIBRARY})