# Raster 模块示例
#
# 这些示例是**对外交付的一部分**：客户拿到的 SDK 里就有它们，
# 演示"如何只用公开头文件调用 DLL"。
#
# Example_CApi 是纯 C 示例：证明第三方用 C / ctypes / cgo 也能消费本模块。

foreach(_example Example_Basic Example_LaserCut Example_Pipeline)
    add_executable(${_example} ${_example}.cpp)
    target_link_libraries(${_example} PRIVATE Raster)
    target_compile_features(${_example} PRIVATE cxx_std_17)
    set_target_properties(${_example} PROPERTIES FOLDER "Examples")
    if(MSVC)
        target_compile_options(${_example} PRIVATE /utf-8 /FS)
    endif()
endforeach()

# --- 纯 C 示例 -------------------------------------------------------------
# C 语言链接 C++ 实现的库：需要显式指定 C++ 链接器，
# 否则 MSVC 会用 C 链接器链接，缺少 C++ 运行时符号。
add_executable(Example_CApi Example_CApi.c)
target_link_libraries(Example_CApi PRIVATE Raster)
set_target_properties(Example_CApi PROPERTIES
    FOLDER "Examples"
    LINKER_LANGUAGE CXX
)
if(MSVC)
    target_compile_options(Example_CApi PRIVATE /utf-8)
endif()
