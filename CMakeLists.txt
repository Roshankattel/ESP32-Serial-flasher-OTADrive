
# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
# # There are issues with ESP-IDF 4.4 and -Wunused-parameter
# if ("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER "4.4")
#     idf_component_get_property(flasher esp-serial-flasher COMPONENT_LIB)
#     target_compile_options(${flasher}
#     PRIVATE
#         -Wunused-parameter
#         -Wshadow)
# endif()
cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(burn-and-test-kit)
SET(EXTRA_COMPONENT_DIRS /)
