# Bakes a directory tree into the exe. Every file under ROOT becomes bytes in
# the binary and an entry in the generated header, reachable from C++ by the
# path it has here. Adding an asset is adding a file: no CMake edit, no new
# include, no table to keep in step.
#
#   reblue_embed_directory(
#       ROOT        <dir>          tree to bake
#       HEADER      <path>         header to generate
#       SOURCES_VAR <var>          receives the generated .cpp list
#       SKIP_DIRS   <sub> ...      subdirectories of ROOT to leave out)
#
# See res/README.md for the tree this is pointed at.
function(reblue_embed_directory)
    cmake_parse_arguments(ARG "" "ROOT;HEADER;SOURCES_VAR" "SKIP_DIRS" ${ARGN})
    if(NOT ARG_ROOT OR NOT ARG_HEADER OR NOT ARG_SOURCES_VAR)
        message(FATAL_ERROR "reblue_embed_directory requires ROOT, HEADER, SOURCES_VAR")
    endif()

    file(GLOB_RECURSE assets RELATIVE "${ARG_ROOT}" CONFIGURE_DEPENDS "${ARG_ROOT}/*")
    list(SORT assets)

    cmake_path(GET ARG_HEADER PARENT_PATH header_dir)
    set(out_dir "${header_dir}/embed")
    set(sources "")
    set(REBLUE_EMBED_DECLS "")
    set(REBLUE_EMBED_ENTRIES "")

    foreach(rel IN LISTS assets)
        set(skipped OFF)
        foreach(dir IN LISTS ARG_SKIP_DIRS)
            if(rel MATCHES "^${dir}/")
                set(skipped ON)
            endif()
        endforeach()
        if(skipped)
            continue()
        endif()

        string(REGEX REPLACE "[^A-Za-z0-9]" "_" ident "${rel}")
        set(symbol "bd_embed_${ident}")
        set(source "${out_dir}/${ident}.cpp")
        set(input "${ARG_ROOT}/${rel}")
        file(SIZE "${input}" size)

        add_custom_command(
            OUTPUT "${source}"
            COMMAND ${CMAKE_COMMAND}
                    -DINPUT=${input} -DSYMBOL=${symbol} -DOUTPUT=${source}
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/embed_one.cmake"
            DEPENDS "${input}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/embed_one.cmake"
            COMMENT "Embedding ${rel}"
            VERBATIM)

        list(APPEND sources "${source}")
        string(APPEND REBLUE_EMBED_DECLS "extern const uint8_t ${symbol}[];\n")
        string(APPEND REBLUE_EMBED_ENTRIES "    {\"${rel}\", ${symbol}, ${size}},\n")
    endforeach()

    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/embedded.h.in" "${ARG_HEADER}" @ONLY)
    set(${ARG_SOURCES_VAR} ${sources} PARENT_SCOPE)
endfunction()
