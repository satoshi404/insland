# Sample project - a complete, minimal game built on the Insland Application
# layer.  Copy this directory (or use scripts/new_project.py) to start a new
# project.

if(ANDROID)
    add_library(Insland_sample SHARED main.cpp)
else()
    add_executable(Insland_sample main.cpp)
endif()

target_link_libraries(Insland_sample PRIVATE insland)

if(WIN32)
    set_target_properties(Insland_sample PROPERTIES OUTPUT_NAME "InslandSample")
endif()
