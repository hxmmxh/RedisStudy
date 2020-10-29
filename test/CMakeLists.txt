cmake_minimum_required (VERSION 3.13)

set(CMAKE_CXX_FLAGS "-pthread")

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

include_directories(../include)

link_directories(${PROJECT_SOURCE_DIR}/lib)

aux_source_directory(. Tests)

foreach(testfile ${Tests})
    get_filename_component(testname ${testfile} NAME_WE)
    add_executable(${testname} ${testfile})
    target_link_libraries(${testname} Log)
endforeach(testfile)