# Everything under src/ compiles into both exes, minus the two carve-outs
# below. CONFIGURE_DEPENDS re-globs when a file appears or disappears, so a new
# source is a new file and nothing else.
file(GLOB_RECURSE reblue_all RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
     CONFIGURE_DEPENDS "*.cpp" "*.h")

# The runtime DXC shader linker has no Vulkan counterpart.
set(reblue_d3d12_only
    gpu/shaders/dxc_link.cpp
    gpu/shaders/dxc_link.h
    gpu/shaders/shader_linker.cpp
    gpu/shaders/shader_linker.h)

set(reblue_installer_only
    installer/disc_install.cpp
    installer/disc_install.h
    installer/installer_wizard.cpp
    installer/installer_wizard.h
    installer/self_install.cpp
    installer/self_install.h
    installer/upgrade_prompt.cpp
    installer/upgrade_prompt.h)

list(REMOVE_ITEM reblue_all ${reblue_d3d12_only} ${reblue_installer_only})

reblue_sources(${reblue_all})
reblue_d3d12_sources(${reblue_d3d12_only})
if(REBLUE_BUILD_INSTALLER)
    reblue_sources(${reblue_installer_only})
endif()
