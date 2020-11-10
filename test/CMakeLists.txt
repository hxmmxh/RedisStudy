cmake_minimum_required (VERSION 3.10)

include_directories(../src)

# aux_source_directory(. Tests)

# foreach(testfile ${Tests})
#    get_filename_component(testname ${testfile} NAME_WE)
#    add_executable(${testname} ${testfile})
#    target_link_libraries(${testname} Log)
# endforeach(testfile)

add_executable(endiantest endianconvTest.c)
target_link_libraries(endiantest RedisStudy)