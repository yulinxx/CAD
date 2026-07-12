cmake_minimum_required(VERSION 4.3)

# KeygenTool - 离线注册码生成工具
# 编译方法：cmake 时设置 -DBUILD_KEYGEN_TOOL=ON

set(KEYGEN_SOURCES
    main.cpp
)

add_executable(KeygenTool ${KEYGEN_SOURCES})

target_include_directories(KeygenTool PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../License/License/Include"
)

target_link_libraries(KeygenTool PRIVATE License)

target_compile_features(KeygenTool PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(KeygenTool PRIVATE /utf-8)
endif()
