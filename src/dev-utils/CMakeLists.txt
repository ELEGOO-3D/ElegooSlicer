option(SLIC3R_ENC_CHECK "Verify encoding of source files" 1)

if (IS_CROSS_COMPILE)
    # Force disable due to cross compilation. This fact is already printed on cli for users
    set(SLIC3R_ENC_CHECK OFF CACHE BOOL "" FORCE)
endif ()

if (SLIC3R_ENC_CHECK)
    add_executable(encoding-check encoding-check.cpp)

    # A global no-op target which depends on all encodings checks,
    # and on which in turn all checked targets depend.
    # This is done to make encoding checks the first thing to be
    # performed before actually compiling any sources of the checked targets
    # to make the check fail as early as possible.
    add_custom_target(global-encoding-check
        ALL
        DEPENDS encoding-check
    )
endif()

# Function that adds source file encoding check to a target
# using the above encoding-check binary

function(encoding_check TARGET)
    if (SLIC3R_ENC_CHECK)
        # Obtain target source files
        get_target_property(T_SOURCES ${TARGET} SOURCES)

        set(STAMP_FILE  "${CMAKE_CURRENT_BINARY_DIR}/encoding-check-${TARGET}.stamp")
        set(RSP_FILE    "${CMAKE_CURRENT_BINARY_DIR}/encoding-check-${TARGET}.rsp")

        # Resolve absolute paths
        set(ABS_SOURCES "")
        foreach(file ${T_SOURCES})
            if(IS_ABSOLUTE "${file}")
                list(APPEND ABS_SOURCES "${file}")
            else()
                list(APPEND ABS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
            endif()
        endforeach()

        # Write response file at configure time
        string(REPLACE ";" "\n" RSP_CONTENT "${ABS_SOURCES}")
        file(WRITE "${RSP_FILE}" "${RSP_CONTENT}")

        # Single process call via response file — no command line length limit
        add_custom_command(
            OUTPUT  "${STAMP_FILE}"
            DEPENDS encoding-check ${ABS_SOURCES}
            COMMENT "Checking source files encodings for target ${TARGET}"
            COMMAND $<TARGET_FILE:encoding-check> ${TARGET} "@${RSP_FILE}"
            COMMAND ${CMAKE_COMMAND} -E touch "${STAMP_FILE}"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )

        add_custom_target(encoding-check-${TARGET} DEPENDS "${STAMP_FILE}")
        add_dependencies(global-encoding-check encoding-check-${TARGET})
        add_dependencies(${TARGET} global-encoding-check)
    endif()
endfunction()

# Function to add POST_BUILD commands for copying sentry crashpad_handler
function(elegooslicer_setup_sentry_handler TARGET)
    if (WIN32)
        set(_handler_name "crashpad_handler.exe")
    else ()
        set(_handler_name "crashpad_handler")
    endif ()

    set(_handler_src "")
    foreach (_p IN LISTS CMAKE_PREFIX_PATH)
        if (NOT _handler_src)
            file(TO_CMAKE_PATH "${_p}" _norm_prefix)
            set(_candidate "${_norm_prefix}/bin/${_handler_name}")
            if (EXISTS "${_candidate}")
                set(_handler_src "${_candidate}")
            endif ()
        endif ()
    endforeach ()

    if (NOT _handler_src)
        message(WARNING "sentry crashpad_handler not found, skipping setup")
        return()
    endif ()

    if (CMAKE_CONFIGURATION_TYPES)
        foreach (CONF ${CMAKE_CONFIGURATION_TYPES})
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/crashpad"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_handler_src}"
                    "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/crashpad/${_handler_name}"
                COMMENT "Copying sentry crashpad_handler to build directory"
                VERBATIM
            )
        endforeach ()
        # Xcode multi-config: also install handler next to the app binary inside the bundle.
        if (CMAKE_MACOSX_BUNDLE)
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${TARGET}>/MacOS/crashpad"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_handler_src}"
                    "$<TARGET_BUNDLE_CONTENT_DIR:${TARGET}>/MacOS/crashpad/${_handler_name}"
                COMMENT "Copying sentry crashpad_handler to macOS app bundle"
                VERBATIM
            )
        endif ()
    else ()
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET}>/crashpad"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_handler_src}"
                "$<TARGET_FILE_DIR:${TARGET}>/crashpad/${_handler_name}"
            COMMENT "Copying sentry crashpad_handler to build directory"
            VERBATIM
        )
        if (CMAKE_MACOSX_BUNDLE)
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${TARGET}>/MacOS/crashpad"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_handler_src}"
                    "$<TARGET_BUNDLE_CONTENT_DIR:${TARGET}>/MacOS/crashpad/${_handler_name}"
                COMMENT "Copying sentry crashpad_handler to macOS app bundle"
                VERBATIM
            )
        endif ()
    endif ()
endfunction()

# Function to add install commands for sentry crashpad_handler
function(elegooslicer_install_sentry_handler)
    if (WIN32)
        set(_handler_name "crashpad_handler.exe")
    else ()
        set(_handler_name "crashpad_handler")
    endif ()

    set(_handler_src "")
    foreach (_p IN LISTS CMAKE_PREFIX_PATH)
        if (NOT _handler_src)
            file(TO_CMAKE_PATH "${_p}" _norm_prefix)
            set(_candidate "${_norm_prefix}/bin/${_handler_name}")
            if (EXISTS "${_candidate}")
                set(_handler_src "${_candidate}")
            endif ()
        endif ()
    endforeach ()

    if (_handler_src)
        if (WIN32)
            install(FILES "${_handler_src}" DESTINATION "crashpad" RENAME "${_handler_name}")
        elseif (CMAKE_MACOSX_BUNDLE)
            install(FILES "${_handler_src}"
                DESTINATION "ElegooSlicer.app/Contents/MacOS/crashpad"
                RENAME "${_handler_name}"
            )
        else ()
            install(FILES "${_handler_src}"
                DESTINATION "${CMAKE_INSTALL_BINDIR}/crashpad"
                RENAME "${_handler_name}"
            )
        endif ()
    endif ()
    unset(_handler_src)
endfunction()
