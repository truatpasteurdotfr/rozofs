install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rozofs.7 DESTINATION share/man/man7 COMPONENT client)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rozofsmount.8 DESTINATION share/man/man8 COMPONENT client)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/storaged.8 DESTINATION share/man/man8 COMPONENT storage)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/storage.conf.5 DESTINATION share/man/man5 COMPONENT storage)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/exportd.8 DESTINATION share/man/man8 COMPONENT export)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/export.conf.5 DESTINATION share/man/man5 COMPONENT export)

#
# add doxygen target
#
if(DOXYGEN)
    add_custom_target(doxygen "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/doxyfile")
else(DOXYGEN)
    message(STATUS "doxygen not found.")
endif(DOXYGEN)