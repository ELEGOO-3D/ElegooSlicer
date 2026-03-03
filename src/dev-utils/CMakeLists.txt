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

        # Define top-level encoding check target for this ${TARGET}
        add_custom_target(encoding-check-${TARGET}
            DEPENDS encoding-check ${T_SOURCES}
            COMMENT "Checking source files encodings for target ${TARGET}"
        )

        # Add checking of each source file as a subcommand of encoding-check-${TARGET}
        foreach(file ${T_SOURCES})
            add_custom_command(TARGET encoding-check-${TARGET} PRE_BUILD
                COMMAND $<TARGET_FILE:encoding-check> ${TARGET} ${file}
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            )
        endforeach()

        # This adds dependency on encoding-check-${TARGET} to ${TARET}
        # via the global-encoding-check
        add_dependencies(global-encoding-check encoding-check-${TARGET})
        add_dependencies(${TARGET} global-encoding-check)
    endif()
endfunction()

# Function to add POST_BUILD commands for copying crashpad_handler
function(elegooslicer_setup_crashpad_handler TARGET)
    if (WIN32)
        if (CMAKE_CONFIGURATION_TYPES)
            # Multi-config generator (Visual Studio)
            foreach (CONF ${CMAKE_CONFIGURATION_TYPES})
                add_custom_command(TARGET ${TARGET} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/crashpad"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_PREFIX_PATH}/bin/crashpad/handler.exe"
                        "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/crashpad/crashpad_handler.exe"
                    COMMENT "Copying crashpad_handler.exe to build directory"
                    VERBATIM
                )
            endforeach ()
        else ()
            # Single-config generator
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET}>/crashpad"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_PREFIX_PATH}/bin/crashpad/handler.exe"
                    "$<TARGET_FILE_DIR:${TARGET}>/crashpad/crashpad_handler.exe"
                COMMENT "Copying crashpad_handler.exe to build directory"
                VERBATIM
            )
        endif ()
    else ()
        # macOS / Linux
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET}>/crashpad"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_PREFIX_PATH}/bin/crashpad/handler"
                "$<TARGET_FILE_DIR:${TARGET}>/crashpad/crashpad_handler"
            COMMENT "Copying crashpad_handler to build directory"
            VERBATIM
        )
        
        # For macOS bundle, also copy to MacOS/crashpad
        if (CMAKE_MACOSX_BUNDLE)
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${TARGET}>/MacOS/crashpad"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_PREFIX_PATH}/bin/crashpad/handler"
                    "$<TARGET_BUNDLE_CONTENT_DIR:${TARGET}>/MacOS/crashpad/crashpad_handler"
                COMMENT "Copying crashpad_handler to macOS app bundle"
                VERBATIM
            )
        endif ()
    endif ()
endfunction()

# Function to add install commands for crashpad_handler
function(elegooslicer_install_crashpad_handler)
    if (WIN32)
        # Install crashpad_handler.exe to crashpad subdirectory
        install(FILES "${CMAKE_PREFIX_PATH}/bin/crashpad/handler.exe"
            DESTINATION "crashpad"
            RENAME "crashpad_handler.exe"
        )
    else ()
        # macOS / Linux
        if (CMAKE_MACOSX_BUNDLE)
            # For macOS bundle, install to MacOS/crashpad
            install(FILES "${CMAKE_PREFIX_PATH}/bin/crashpad/handler"
                DESTINATION "ElegooSlicer.app/Contents/MacOS/crashpad"
                RENAME "crashpad_handler"
            )
        else ()
            # For Linux and non-bundle macOS, install to bin/crashpad
            install(FILES "${CMAKE_PREFIX_PATH}/bin/crashpad/handler"
                DESTINATION "${CMAKE_INSTALL_BINDIR}/crashpad"
                RENAME "crashpad_handler"
            )
        endif ()
    endif ()
endfunction()
