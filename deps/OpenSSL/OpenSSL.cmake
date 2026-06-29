
include(ProcessorCount)
ProcessorCount(NPROC)

if(DEFINED OPENSSL_ARCH)
    set(_cross_arch ${OPENSSL_ARCH})
else()
    if(WIN32)
        if("${CMAKE_GENERATOR_PLATFORM}" STREQUAL "ARM64")
            set(_cross_arch "VC-WIN64-ARM")
        else()
            set(_cross_arch "VC-WIN64A")
        endif()
    elseif(APPLE)
        set(_cross_arch "darwin64-${CMAKE_OSX_ARCHITECTURES}-cc")
	endif()
endif()

if(WIN32)
    set(_conf_cmd perl Configure )
    set(_cross_comp_prefix_line "")
    set(_make_cmd nmake /E)
    set(_install_cmd nmake /E install_sw )
else()
    if(APPLE)
        set(_conf_cmd export MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET} && ./Configure -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET})
    else()
        set(_conf_cmd env "CC=${CMAKE_C_COMPILER}" "LDFLAGS=${CMAKE_EXE_LINKER_FLAGS}" "./config")
    endif()
    set(_cross_comp_prefix_line "")
    set(_make_cmd make -j${NPROC})
    set(_install_cmd make -j${NPROC} install_sw)
    if (CMAKE_CROSSCOMPILING)
        set(_cross_comp_prefix_line "--cross-compile-prefix=${TOOLCHAIN_PREFIX}-")

        if (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64" OR ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm64")
            set(_cross_arch "linux-aarch64")
        elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "armhf") # For raspbian
            # TODO: verify
            set(_cross_arch "linux-armv4")
        endif ()
    endif ()
endif()

ExternalProject_Add(dep_OpenSSL
    #EXCLUDE_FROM_ALL ON
    # URL "https://github.com/openssl/openssl/archive/OpenSSL_1_1_1w.tar.gz"
    # URL_HASH SHA256=2130E8C2FB3B79D1086186F78E59E8BC8D1A6AEDF17AB3907F4CB9AE20918C41

    URL "https://github.com/openssl/openssl/archive/refs/tags/openssl-3.1.8.zip"
    URL_HASH SHA256=bbd5cbd8cc8ea852d31c001a9b767eadef0548b098e132b580a1f0c80d1778b7
    DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/OpenSSL
	CONFIGURE_COMMAND ${_conf_cmd} ${_cross_arch}
        "--openssldir=${DESTDIR}"
        "--prefix=${DESTDIR}"
        ${_cross_comp_prefix_line}
        no-shared
        no-asm
        no-ssl3-method
        no-dynamic-engine
    BUILD_IN_SOURCE ON
    BUILD_COMMAND ${_make_cmd}
    INSTALL_COMMAND ${_install_cmd}
)

ExternalProject_Add_Step(dep_OpenSSL install_cmake_files
    DEPENDEES install

    COMMAND ${CMAKE_COMMAND} -E copy_directory openssl "${DESTDIR}${CMAKE_INSTALL_LIBDIR}/cmake/openssl"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

# On x86_64 Linux, OpenSSL 3.x installs to lib64 by default, but we need it in lib
# Copy the libraries from lib64 to lib if they exist
if(NOT WIN32 AND NOT APPLE)
    ExternalProject_Add_Step(dep_OpenSSL copy_lib64_to_lib
        DEPENDEES install_cmake_files
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DESTDIR}/lib64/libssl.a" "${DESTDIR}/lib/libssl.a"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DESTDIR}/lib64/libcrypto.a" "${DESTDIR}/lib/libcrypto.a"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${DESTDIR}/lib64/pkgconfig" "${DESTDIR}/lib/pkgconfig"
    )
endif()
