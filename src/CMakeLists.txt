cmake_minimum_required (VERSION 3.10)

# aux_source_directory(. RedisStudy_srcs)

add_library(RedisStudy STATIC xmendianconv.c xmmalloc.c xmsds.c xmadlist.c xmdict.c xmobject.c xmskiplist.c 
            xmintset.c xmzplist.c )

# add_library(Log STATIC ${Log_srcs})