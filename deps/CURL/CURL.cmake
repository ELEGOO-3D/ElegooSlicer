find_package(CURL QUIET)

if (USE_SYSTEM_DEPS AND CURL_FOUND)
    message(STATUS "Using system CURL")
    set(CURL_PKG "")
else()
    message(STATUS "Building CURL as external project")
    set(_curl_platform_flags
      -DENABLE_IPV6:BOOL=ON
      -DENABLE_VERSIONED_SYMBOLS:BOOL=ON
      -DENABLE_THREADED_RESOLVER:BOOL=ON
      -DENABLE_MANUAL:BOOL=OFF
      -DCURL_DISABLE_LDAP:BOOL=ON
      -DCURL_DISABLE_LDAPS:BOOL=ON
      -DCURL_DISABLE_RTSP:BOOL=ON
      -DCURL_DISABLE_DICT:BOOL=ON
      -DCURL_DISABLE_TELNET:BOOL=ON
      -DCURL_DISABLE_POP3:BOOL=ON
      -DCURL_DISABLE_IMAP:BOOL=ON
      -DCURL_DISABLE_SMB:BOOL=ON
      -DCURL_DISABLE_SMTP:BOOL=ON
      -DCURL_DISABLE_GOPHER:BOOL=ON
      -DCURL_DISABLE_TFTP:BOOL=ON
      -DCURL_DISABLE_MQTT:BOOL=ON

      -DCMAKE_USE_GSSAPI:BOOL=OFF
      -DCMAKE_USE_LIBSSH2:BOOL=OFF
      -DUSE_RTMP:BOOL=OFF
      -DUSE_NGHTTP2:BOOL=OFF
      -DUSE_MBEDTLS:BOOL=OFF
    )

    if (WIN32)
      set(_curl_platform_flags  ${_curl_platform_flags} -DCMAKE_USE_OPENSSL=ON -DCURL_CA_PATH:STRING=none)
    elseif (APPLE)
      set(_curl_platform_flags
        ${_curl_platform_flags}
        -DCMAKE_USE_OPENSSL:BOOL=ON
        -DCURL_CA_PATH:STRING=none
      )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      set(_curl_platform_flags
        ${_curl_platform_flags}
        -DCMAKE_USE_OPENSSL:BOOL=ON
        -DCURL_CA_PATH:STRING=none
        -DCURL_CA_BUNDLE:STRING=none
        -DCURL_CA_FALLBACK:BOOL=ON
      )
    endif ()

    elegooslicer_add_cmake_project(CURL SHARED_LIBS_BOOL TRUE
      URL                 https://github.com/curl/curl/archive/refs/tags/curl-7_75_0.zip
      URL_HASH            SHA256=a63ae025bb0a14f119e73250f2c923f4bf89aa93b8d4fafa4a96a765a
      DEPENDS             ${ZLIB_PKG}
      CMAKE_ARGS
        -DBUILD_TESTING:BOOL=OFF
        -DBUILD_CURL_EXE:BOOL=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        ${_curl_platform_flags}
    )

    if(NOT OPENSSL_FOUND)
      add_dependencies(dep_CURL ${OPENSSL_PKG})
    endif()

    if (MSVC)
        add_debug_dep(dep_CURL)
    endif ()
    set(CURL_PKG dep_CURL)
endif()