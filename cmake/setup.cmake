set (WDIR "" CACHE FORCE "Working Directory")
set (PREFIX "" CACHE FORCE "Prefix")
set (SETUP "" CACHE FORCE "Path to setup.py")
set (INSTALL_ROOT $ENV{DESTDIR})
set (EXTRA_ARGS "")

if (INSTALL_ROOT)
    set (INSTALL_ROOT_ARGS "--root=$ENV{DESTDIR}")
    set (EXTRA_ARGS "--install-layout=deb")
else (INSTALL_ROOT)
    set (INSTALL_ROOT_ARGS "")
endif (INSTALL_ROOT)

cmake_policy (SET CMP0012 NEW)

execute_process (COMMAND python ${SETUP} install ${EXTRA_ARGS} --prefix=${PREFIX} ${INSTALL_ROOT_ARGS} WORKING_DIRECTORY ${WDIR})