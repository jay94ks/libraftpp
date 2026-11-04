foreach(file LICENSE LICENSE.Apache LICENSE.leveldb license.terms LICENSE.md LICENSE.txt LICENSE_1_0.txt NOTICE NOTICE.txt COPYING COPYING.LESSER COPYING.LIB README lib/LICENSE lib/COPYING)
    if(EXISTS "${RAFTPP_SDK_SOURCE}/${file}")
        get_filename_component(directory "${file}" DIRECTORY)
        file(INSTALL "${RAFTPP_SDK_SOURCE}/${file}"
            DESTINATION "${RAFTPP_SDK_PREFIX}/share/licenses/${RAFTPP_SDK_NAME}/${directory}")
    endif()
endforeach()

if(RAFTPP_SDK_NAME STREQUAL "msquic")
    foreach(tls IN ITEMS openssl quictls)
        foreach(file IN ITEMS LICENSE.txt LICENSE NOTICE NOTICE.txt)
            if(EXISTS "${RAFTPP_SDK_SOURCE}/submodules/${tls}/${file}")
                file(INSTALL "${RAFTPP_SDK_SOURCE}/submodules/${tls}/${file}"
                    DESTINATION "${RAFTPP_SDK_PREFIX}/share/licenses/msquic/${tls}")
            endif()
        endforeach()
    endforeach()
endif()
