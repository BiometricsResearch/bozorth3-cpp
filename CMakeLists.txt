cmake_minimum_required(VERSION 3.10)
project(bz)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Weverything -Wno-c++98-compat /EHa -mavx2 -flto=thin")

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /DEBUG")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -march=native ")
#set(CMAKE_CXX_FLA GS "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize=undefined -O1 -fno-omit-frame-pointer -g")

include_directories(external)
include_directories(external/cxxopts/include)
include_directories(external/GSL/include)
include_directories(src/bozorth3)

add_executable(bench
        src/utils.cpp
        src/bozorth3/bozorth3.cpp
        src/bench.cpp
        src/bozorth3/pair_holder.cpp)

add_executable(bz3
        src/utils.cpp
        src/bozorth3/bozorth3.cpp
        src/bozorth3/pair_holder.cpp
        src/bz3.cpp)

if (UNIX)
    target_link_libraries(bench stdc++fs)
    target_link_libraries(bz3 stdc++fs pthread)
endif ()