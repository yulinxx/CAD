cmake_minimum_required(VERSION 3.20)

# KeygenTool - 离线注册码生成工具
# 编译方法：cmake 时设置 -DBUILD_KEYGEN_TOOL=ON
# 或手动编译：cl /EHsc main.cpp /I../../Main/Src /link openssl.lib

set(KEYGEN_SOURCES
    main.cpp
    ../../Main/Src/License/KeygenTool.cpp
)

add_executable(KeygenTool ${KEYGEN_SOURCES})

target_include_directories(KeygenTool PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Main/Src"
)

find_package(OpenSSL REQUIRED)
target_link_libraries(KeygenTool PRIVATE OpenSSL::SSL OpenSSL::Crypto)

target_compile_features(KeygenTool PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(KeygenTool PRIVATE /utf-8)
endif()
