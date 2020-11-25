cmake_minimum_required (VERSION 3.10)
project(RedisStudy)

set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)
link_directories(${PROJECT_BINARY_DIR}/lib)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_C_FLAGS "-g -Wall")
# set(CMAKE_CXX_FLAGS "-g -Wall")
add_subdirectory(src)
add_subdirectory(test)

