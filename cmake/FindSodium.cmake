# Fizz's exported package requires a sodium target but does not install its
# finder. Resolve the SDK without embedding the producer's installation path.
find_path(Sodium_INCLUDE_DIR sodium.h)
find_library(Sodium_LIBRARY NAMES sodium libsodium)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Sodium REQUIRED_VARS Sodium_INCLUDE_DIR Sodium_LIBRARY)
if(Sodium_FOUND AND NOT TARGET sodium)
    add_library(sodium UNKNOWN IMPORTED)
    set_target_properties(sodium PROPERTIES IMPORTED_LOCATION "${Sodium_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Sodium_INCLUDE_DIR}")
endif()
mark_as_advanced(Sodium_INCLUDE_DIR Sodium_LIBRARY)
