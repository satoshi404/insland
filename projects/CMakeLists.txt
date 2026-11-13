# Standalone projects live under projects/<name>/ and provide their own
# CMakeLists.txt.  Directories without one (scratch spaces, notes, ...) are
# ignored, so the build stays green while a project is being sketched.
# CONFIGURE_DEPENDS makes the build system re-run this glob, so a project
# created by scripts/new_project.py is picked up on the next build without
# manually reconfiguring.
file(GLOB _insland_project_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*")

foreach(_insland_project_dir IN LISTS _insland_project_dirs)
    if(IS_DIRECTORY "${_insland_project_dir}"
       AND EXISTS "${_insland_project_dir}/CMakeLists.txt")
        add_subdirectory("${_insland_project_dir}")
    endif()
endforeach()
