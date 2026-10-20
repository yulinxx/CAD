cmake_minimum_required(VERSION 4.3)

# KeygenTool - 离线注册码生成工具
# 编译方法：cmake 时设置 -DBUILD_KEYGEN_TOOL=ON

set(KEYGEN_SOURCES
    main.cpp
    ../../Licensing/License/Src/KeygenTool.cpp
    ../../Licensing/License/Src/LicenseKeygenDLL.cpp
)

add_executable(KeygenTool ${KEYGEN_SOURCES})

# 定义导出宏后 keygen 函数直接编译进本可执行文件，
# 避免链接不随主程序分发的 License DLL 导入符号
target_compile_definitions(KeygenTool PRIVATE LICENSE_KEYGEN_EXPORTS)

target_include_directories(KeygenTool PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Licensing/License/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Licensing/License/Src"
)

find_package(OpenSSL CONFIG REQUIRED)
target_link_libraries(KeygenTool PRIVATE OpenSSL::Crypto)

target_compile_features(KeygenTool PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(KeygenTool PRIVATE /utf-8)
endif()
