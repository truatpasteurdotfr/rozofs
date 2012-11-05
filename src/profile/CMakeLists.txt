include_directories(${CMAKE_BINARY_DIR}) # for config.h
include_directories(${CMAKE_SOURCE_DIR}) # for librozofs headers
link_directories(${CMAKE_BINARY_DIR}/rozofs)

add_executable(rprof
    rprofile.c
)
target_link_libraries(rprof rozofs)

# Install

install(TARGETS rprof
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    COMPONENT profile
)
