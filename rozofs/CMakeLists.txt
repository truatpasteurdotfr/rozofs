include_directories(${CMAKE_BINARY_DIR}) # for config.h
include_directories(${CMAKE_SOURCE_DIR})

set (librozofs_sources
    rozofs.h
    rozofs.c
    common/types.h
    common/daemon.h
    common/daemon.c
    common/dist.h
    common/htable.h
    common/htable.c
    common/list.h
    common/log.h
    common/mattr.h
    common/mattr.c
    common/transform.h
    common/transform.c
    common/xmalloc.h
    common/xmalloc.c
    rpc/eproto.h
    rpc/eprotoxdr.c
    rpc/eprotosvc.c
    rpc/eprotoclt.c
    rpc/sproto.h
    rpc/sprotoxdr.c
    rpc/sprotosvc.c
    rpc/sprotoclt.c
    rpc/mproto.h
    rpc/mprotoxdr.c
    rpc/mprotosvc.c
    rpc/mprotoclt.c
    rpc/spproto.h
    rpc/spprotoxdr.c
    rpc/spprotosvc.c
    rpc/spprotoclt.c
    rpc/epproto.h
    rpc/epprotoxdr.c
    rpc/epprotosvc.c
    rpc/epprotoclt.c
    rpc/mpproto.h
    rpc/mpprotoxdr.c
    rpc/mpprotosvc.c
    rpc/mpprotoclt.c
    rpc/rpcclt.h
    rpc/rpcclt.c
    rpc/eclient.h
    rpc/eclient.c
    rpc/sclient.h
    rpc/sclient.c
    rpc/mclient.h
    rpc/mclient.c
    rpc/spclient.h
    rpc/spclient.c
    rpc/epclient.h
    rpc/epclient.c
    rpc/mpclient.h
    rpc/mpclient.c
)

add_library(rozofs STATIC ${librozofs_sources})
target_link_libraries(rozofs ${UUID_LIBRARY})

