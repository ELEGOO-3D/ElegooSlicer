
orcaslicer_add_cmake_project(ixwebsocket
    URL https://github.com/machinezone/IXWebSocket/archive/refs/tags/v11.4.6.zip
        URL_HASH SHA256=99f9288e0f742ad08bc60d99abeef6169a3bf137f9c1ef1cca5d39ffbbe9d44b

    DEPENDS ${OPENSSL_PKG}
    CMAKE_ARGS 
        -DUSE_TLS:BOOL=ON
        -DUSE_OPEN_SSL:BOOL=ON
)