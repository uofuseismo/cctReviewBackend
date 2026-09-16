#.rst:
# FindOpenLDAP
# ------------
# 
# Finds the OpenLDAP library and include.
#
# Imported targets
# ^^^^^^^^^^^^^^^^
#
# ``OpenLDAP_FOUND``
#   True indicates OpenLDAP and OpenLBER was found.
# ``OpenLDAP::ldap``
#   The OpenLDAP library, if found. 
# ``OpenLDAP::lber``
#   The OpenLBER library, if found.

# Already in cache, be silent
if (OpenLDAP_INCLUDE_DIR AND OpenLDAP_LIBRARY AND OpenLBER_LIBRARY)
    set(OpenLDAP_FIND_QUIETLY TRUE)
endif()

find_path(OpenLDAP_INCLUDE_DIR ldap.h PATHS
          /usr/include
          /opt/local/include
          /usr/local/include)

if (BUILD_SHARED_LIBS)
   message("Looking for LDAP shared libraries")
   find_library(OpenLDAP_LIBRARY
                NAMES libldap.so
                PATHS $ENV{LDAP_DIR}/lib/
                      $ENV{LDAP_DIR}/
                      /usr/local/lib64
                      /usr/local/lib
               )
   find_library(OpenLBER_LIBRARY
                NAMES liblber.so
                PATHS $ENV{LDAP_DIR}/lib/
                      $ENV{LDAP_DIR}/
                      $ENV{LBER_DIR}/lib/
                      $ENV{LBER_DIR}/
                      /usr/local/lib64
                      /usr/local/lib
               )
else()
   message("Looking for LDAP static libraries")
   find_library(OpenLDAP_LIBRARY
                NAMES libldap.a
                PATHS $ENV{LDAP_DIR}/lib/
                      $ENV{LDAP_DIR}/
                      /usr/local/lib64
                      /usr/local/lib
               )
   find_library(OpenLBER_LIBRARY
                NAMES liblber.a
                PATHS $ENV{LDAP_DIR}/lib/
                      $ENV{LDAP_DIR}/
                      $ENV{LBER_DIR}/lib/
                      $ENV{LBER_DIR}/
                      /usr/local/lib64
                      /usr/local/lib
               )
endif()

#include(FindPackageHandleStandardArgs)
#find_package_handle_standard_args(OpenLDAP DEFAULT_MSG
#                                  OpenLDAP_INCLUDE_DIR LDAP_LIBRARY LBER_LIBRARY)
#
#set(OpenLDAP_LIBRARIES ${LDAP_LIBRARY} ${LBER_LIBRARY})
#
#mark_as_advanced(OpenLDAP_INCLUDE_DIR LDAP_LIBRARY LBER_LIBRARY OpenLDAP_LIBRARIES)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenLDAP
                                  FOUND_VAR OpenLDAP_FOUND
                                  REQUIRED_VARS OpenLDAP_LIBRARY OpenLBER_LIBRARY OpenLDAP_INCLUDE_DIR)
#                                  VERSION_VAR ${OpenLDAP_VERSION})
if (OpenLDAP_FOUND AND NOT TARGET OpenLDAP::ldap)
   add_library(OpenLDAP::ldap UNKNOWN IMPORTED)
   set_target_properties(OpenLDAP::ldap PROPERTIES
                         IMPORTED_LOCATION "${OpenLDAP_LIBRARY}"
                         INTERFACE_INCLUDE_DIRECTORIES "${OpenLDAP_INCLUDE_DIR}")

endif()
if (OpenLDAP_FOUND AND NOT TARGET OpenLDAP::lber)
   add_library(OpenLDAP::lber UNKNOWN IMPORTED)
   set_target_properties(OpenLDAP::lber PROPERTIES
                         IMPORTED_LOCATION "${OpenLBER_LIBRARY}"
                         INTERFACE_INCLUDE_DIRECTORIES "${OpenLDAP_INCLUDE_DIR}")

endif()
mark_as_advanced(OpenLDAP_INCLUDE_DIR OpenLDAP_LIBRARY OpenLBER_LIBRARY OpenLBER_LIBRARY)

