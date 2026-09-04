cmake_minimum_required(VERSION 3.28)

project(mq_core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dobby as static (no runtime dependency)
set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "static dobby" FORCE)
add_subdirectory(dobby dobby)
if(ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options(dobby PRIVATE -fomit-frame-pointer)
endif()

# LSPlant (pulls in dex_builder)
add_subdirectory(lsplant_jni lsplant_build)

add_library(mq-core SHARED glue.cpp)

target_include_directories(mq-core PRIVATE
    dobby/include
    lsplant_jni/include
)

target_link_libraries(mq-core PRIVATE dobby lsplant_static log)
