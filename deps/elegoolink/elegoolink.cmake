
function(parse_dotenv path)
    if(EXISTS "${path}")
        file(READ "${path}" _dotenv_content)
        string(REPLACE "\r\n" "\n" _dotenv_content "${_dotenv_content}")
        string(REPLACE "\r" "\n" _dotenv_content "${_dotenv_content}")
        string(REGEX MATCHALL "[^\n]+" _lines "${_dotenv_content}")
        foreach(_line IN LISTS _lines)
            string(REGEX REPLACE "^\\s*#.*" "" _trimmed "${_line}") 
            string(STRIP "${_trimmed}" _trimmed)
            if(_trimmed STREQUAL "") 
                continue()
            endif()
            string(REGEX MATCH "^([A-Za-z_][A-Za-z0-9_]*)=(.*)$" _m "${_trimmed}")
            if(_m)
                set(_key "${CMAKE_MATCH_1}")
                set(_val "${CMAKE_MATCH_2}")
                string(REGEX REPLACE "^\"(.*)\"$" "\\1" _val "${_val}")
                string(REGEX REPLACE "^'(.*)'$" "\\1" _val "${_val}")
                set("${_key}" "${_val}" PARENT_SCOPE)
            endif()
        endforeach()
    endif()
endfunction()

if(DEFINED ELEGOO_INTERNAL_TESTING AND ELEGOO_INTERNAL_TESTING STREQUAL "1")
    parse_dotenv("${CMAKE_SOURCE_DIR}/../.env.testing")
else()
    parse_dotenv("${CMAKE_SOURCE_DIR}/../.env")
endif()

elegooslicer_add_cmake_project(elegoolink

    GIT_REPOSITORY      https://github.com/ELEGOO-3D/elegoo-link.git
    GIT_TAG             v1.0.0
    
    DEPENDS 
        dep_ixwebsocket 
        dep_PahoMqttCpp
        dep_CURL
        ${OPENSSL_PKG}

    CMAKE_ARGS 
        -DBUILD_EXAMPLES=OFF
        -DBUILD_TESTS=OFF
        -DBUILD_SHARED_LIBS=OFF
        -DENABLE_CLOUD_FEATURES=ON
        -DAGORA_APP_ID=${AGORA_APP_ID}
        -DELEGOO_CHINA_IOT_URL=${ELEGOO_CHINA_IOT_URL}
        -DELEGOO_GLOBAL_IOT_URL=${ELEGOO_GLOBAL_IOT_URL}
        -DELEGOO_INTERNAL_TESTING=${ELEGOO_INTERNAL_TESTING}
)