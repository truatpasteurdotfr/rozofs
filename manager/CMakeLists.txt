if (NOT PYTHON_NOTFOUND)

    set(DEPS        "${CMAKE_CURRENT_SOURCE_DIR}/rozofs/__init__.py")
    set(OUTPUT      "${CMAKE_CURRENT_BINARY_DIR}/timestamp")

    add_custom_command(OUTPUT ${OUTPUT}
                       COMMAND ${PYTHON} setup.py build
                       COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT}
                       DEPENDS ${DEPS})

    add_custom_target(manager ALL DEPENDS ${OUTPUT})

    install(CODE "execute_process (COMMAND cmake -DSETUP=${CMAKE_BINARY_DIR}/manager/setup.py -DDEBIAN=${DEBIAN_LAYOUT} -DPREFIX=${CMAKE_INSTALL_PREFIX} -DWDIR=${CMAKE_CURRENT_BINARY_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/setup.cmake)")

endif(NOT PYTHON_NOTFOUND)
