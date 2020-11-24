cmake_minimum_required (VERSION 3.10)
project(RedisStudy)

set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)
link_directories(${PROJECT_BINARY_DIR}/lib)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

add_subdirectory(src)
add_subdirectory(test)

