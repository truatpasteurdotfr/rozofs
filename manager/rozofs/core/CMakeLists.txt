include_directories(${CMAKE_BINARY_DIR}) # for config.h
include_directories(${CMAKE_SOURCE_DIR})
link_directories(${CMAKE_BINARY_DIR}/rozofs)

add_library(_libconfig SHARED libconfig_wrap.c)
target_link_libraries(_libconfig ${CONFIG_LIBRARY})
set_target_properties(_libconfig PROPERTIES COMPILE_FLAGS "-fPIC")

add_library(_profile SHARED profile_wrap.c)
target_link_libraries(_profile rozofs)
set_target_properties(_profile PROPERTIES COMPILE_FLAGS "-fPIC")