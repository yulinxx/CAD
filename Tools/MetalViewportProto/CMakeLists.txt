# Metal 原生视口原型（仅 Apple 平台）
#
# 这不是产品代码，是一次性的可行性验证：产品里的 2D 视口是 QOpenGLWidget，
# 它的 NSView 由 Qt 与 GL 上下文共同掌管，不能挂 CAMetalLayer，因此「把
# (NSView*)winId() 交给 Metal」这条路上不去。本原型把两条候选宿主路径都跑
# 一遍，用实际表现来定方案。
#
# 默认不构建：-DBUILD_METAL_VIEWPORT_PROTO=ON

if(NOT APPLE)
    message(STATUS "  MetalViewportProto: 非 Apple 平台无意义，已跳过")
    return()
endif()

# 两份 RenderX 二选一：
#   - 默认链接同树目标 RenderX（正常情况）
#   - 传 -DMETAL_PROTO_RENDERX_LIB=<dylib 绝对路径> 则改为链接一份预构建库。
#     原型要验证的是**宿主侧原生窗口**能否把 NSView 交给 Metal，不需要新的
#     Renderx 代码；当同树 Renderx 正被其他工作流编辑而暂时编不过时，
#     用一份已知可用的 dylib 就能独立验证，不必等。
set(METAL_PROTO_RENDERX_LIB "" CACHE FILEPATH
    "链接预构建的 RenderX 动态库（留空则链接同树 RenderX 目标）")

if(METAL_PROTO_RENDERX_LIB)
    add_library(MetalProtoRenderX SHARED IMPORTED)
    set_target_properties(MetalProtoRenderX PROPERTIES
        IMPORTED_LOCATION "${METAL_PROTO_RENDERX_LIB}"
    )
    set(_renderx_lib MetalProtoRenderX)
    message(STATUS "  MetalViewportProto: 链接预构建 RenderX -> ${METAL_PROTO_RENDERX_LIB}")
elseif(TARGET RenderX)
    set(_renderx_lib RenderX)
else()
    message(STATUS "  MetalViewportProto: 需要 BUILD_RENDERX=ON，或指定 METAL_PROTO_RENDERX_LIB")
    return()
endif()

sanyi_find_qt(Widgets)

add_executable(MetalViewportProto main.cpp)

target_compile_features(MetalViewportProto PRIVATE cxx_std_17)

target_include_directories(MetalViewportProto PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Renderx/include"
)

# 字体文件路径以宏注入：DLL 自己不做文件 IO，宿主负责把字节读进来。
# 这里用 Renderx 自带的那份测试字体（ASCII），避免依赖 qrc。
target_compile_definitions(MetalViewportProto PRIVATE
    PROTO_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../Renderx/src/res/default_screen_font.ttf"
)

target_link_libraries(MetalViewportProto PRIVATE
    ${_renderx_lib}
    ${QT_LIBRARIES}
)

set_target_properties(MetalViewportProto PROPERTIES FOLDER "Tools")

message(STATUS "  MetalViewportProto: ENABLED")
