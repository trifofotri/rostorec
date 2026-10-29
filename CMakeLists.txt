cmake_minimum_required(VERSION 3.14)
project(rostorec C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL REQUIRED)

file(GLOB SRC CONFIGURE_DEPENDS src/*.c)

add_library(lz4 STATIC
    third_party/lz4/lib/lz4.c
    third_party/lz4/lib/lz4hc.c
    third_party/lz4/lib/lz4frame.c
    third_party/lz4/lib/xxhash.c
)

add_library(cjson STATIC third_party/cjson/cJSON.c)

target_include_directories(lz4 PUBLIC third_party/lz4/lib)
target_include_directories(cjson PUBLIC third_party/cjson)

add_executable(${PROJECT_NAME} ${SRC})

target_include_directories(${PROJECT_NAME} PRIVATE include)

target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl lz4 cjson)