cmake_minimum_required(VERSION 3.22)

project(qfunqaux_zygisk CXX)

add_library(zygisk SHARED src/module.cpp)

target_include_directories(zygisk PRIVATE include)

target_compile_features(zygisk PRIVATE cxx_std_17)

target_compile_options(zygisk PRIVATE -fvisibility=hidden -fno-rtti)

target_link_libraries(zygisk PRIVATE log)

if(DEFINED MQ_VERCODE)
    target_compile_definitions(zygisk PRIVATE MQ_VERCODE=${MQ_VERCODE})
endif()
