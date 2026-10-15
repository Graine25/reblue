# Recompiles the Xenos shaders under INPUT_DIR at build time into OUTPUT_CPP,
# which TARGET compiles. REBLUE_SHADERGEN runs rexglue-shadergen's xr-reblue
# profile, which writes the cache XenosRecomp did, and OFF runs RECOMP_TARGET.
# Defines reblue_shader_cache_gen, and reblue_shader_hlsl_dump for inspecting
# the intermediate HLSL.
function(reblue_shader_cache)
    cmake_parse_arguments(ARG ""
        "TARGET;RECOMP_TARGET;INPUT_DIR;INCLUDE_FILE;OUTPUT_CPP" "" ${ARGN})
    foreach(arg TARGET RECOMP_TARGET INPUT_DIR INCLUDE_FILE OUTPUT_CPP)
        if(NOT ARG_${arg})
            message(FATAL_ERROR "reblue_shader_cache: missing ${arg}")
        endif()
    endforeach()

    set(REBLUE_HLSL_DUMP_DIR "${CMAKE_BINARY_DIR}/hlsl_dump" CACHE PATH
        "Directory reblue_shader_hlsl_dump writes recompiled HLSL into")

    if(REBLUE_SHADERGEN)
        # The tool's depfile lists every file and folder it scanned, so an added
        # or changed asset reruns it without a reconfigure. INCLUDE_FILE is only
        # checked against the profile, not read. XenosRecomp's cache layout has
        # no part units, and the per-shader artefact cache stays in the build
        # tree rather than the user's home. REBLUE_SHADERGEN_OVERLAY holds
        # reblue's semantic choices over the profile (shadergen.toml).
        rexglue_add_shader_cache(reblue_shader_cache
            INPUTS       "${ARG_INPUT_DIR}"
            PROFILE      xr-reblue
            OVERLAYS     ${REBLUE_SHADERGEN_OVERLAY}
            OUTPUT       "${ARG_OUTPUT_CPP}"
            PARTS        0
            HOST_INCLUDE "${ARG_INCLUDE_FILE}"
            CACHE_DIR    "${CMAKE_BINARY_DIR}/shadergen-cache"
            DUMP_DIR     "${REBLUE_HLSL_DUMP_DIR}"
            TARGETS      ${ARG_TARGET})
        add_custom_target(reblue_shader_hlsl_dump)
        add_dependencies(reblue_shader_hlsl_dump reblue_shader_cache_dump)
        return()
    endif()

    cmake_path(GET ARG_OUTPUT_CPP PARENT_PATH output_dir)
    file(MAKE_DIRECTORY "${output_dir}")

    file(GLOB_RECURSE shader_inputs CONFIGURE_DEPENDS
        "${ARG_INPUT_DIR}/*.vso" "${ARG_INPUT_DIR}/*.pso" "${ARG_INPUT_DIR}/*.xex")

    add_custom_command(
        OUTPUT "${ARG_OUTPUT_CPP}"
        COMMAND $<TARGET_FILE:${ARG_RECOMP_TARGET}>
                "${ARG_INPUT_DIR}" "${ARG_OUTPUT_CPP}" "${ARG_INCLUDE_FILE}"
        DEPENDS ${ARG_RECOMP_TARGET} "${ARG_INCLUDE_FILE}" ${shader_inputs}
        COMMENT "Recompiling Xenos shaders from ${ARG_INPUT_DIR}"
        USES_TERMINAL VERBATIM)
    add_custom_target(reblue_shader_cache_gen DEPENDS "${ARG_OUTPUT_CPP}")
    target_sources(${ARG_TARGET} PRIVATE "${ARG_OUTPUT_CPP}")
    add_dependencies(${ARG_TARGET} reblue_shader_cache_gen)

    add_custom_target(reblue_shader_hlsl_dump
        COMMAND ${CMAKE_COMMAND} -E make_directory "${REBLUE_HLSL_DUMP_DIR}"
        COMMAND $<TARGET_FILE:${ARG_RECOMP_TARGET}>
                "${ARG_INPUT_DIR}" "${output_dir}/shader_cache.hlsldump.cpp"
                "${ARG_INCLUDE_FILE}" "${REBLUE_HLSL_DUMP_DIR}"
        COMMENT "Dumping recompiled HLSL to ${REBLUE_HLSL_DUMP_DIR}"
        USES_TERMINAL VERBATIM)
    add_dependencies(reblue_shader_hlsl_dump ${ARG_RECOMP_TARGET})
endfunction()
