# The pinned Folly snapshot compiles Linux AIO whenever libaio.h is visible,
# even when LibAIO discovery is disabled. Neither mvfst nor this adapter uses
# those components. Trim them from our SDK profile instead of acquiring an
# unrelated disk-I/O dependency or leaving unresolved symbols in libfolly.so.
function(raftpp_folly_sdk_profile)
    if(NOT TARGET folly)
        message(FATAL_ERROR "The pinned Folly SDK no longer provides target folly")
    endif()
    get_target_property(sources folly SOURCES)
    foreach(component IN ITEMS async_io simple_async_io)
        set(object "folly_io_async_${component}_obj")
        if(NOT TARGET ${object})
            message(FATAL_ERROR "The pinned Folly SDK no longer provides ${object}")
        endif()
        list(REMOVE_ITEM sources "$<TARGET_OBJECTS:${object}>")
        set_target_properties(${object} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endforeach()
    set_property(TARGET folly PROPERTY SOURCES "${sources}")
endfunction()
cmake_language(DEFER CALL raftpp_folly_sdk_profile)
