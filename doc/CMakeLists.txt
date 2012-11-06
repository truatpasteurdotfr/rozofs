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