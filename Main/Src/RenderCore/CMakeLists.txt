cmake_minimum_required(VERSION 3.20)

set(TARGET_NAME UiRenderCore)

# 源文件
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(HEADERS
    ${SRC_DIR}/RenderCoreApi.h
    ${SRC_DIR}/RenderTypes.h
    ${SRC_DIR}/RenderContext.h
    ${SRC_DIR}/RenderFrame.h
    ${SRC_DIR}/IRenderBackend.h
    ${SRC_DIR}/SceneCompiler.h
    ${SRC_DIR}/DefaultSceneCompiler.h
    ${SRC_DIR}/RenderBackendFactory.h
    ${SRC_DIR}/MinimalOpenGLBackend.h
    ${SRC_DIR}/RenderCoreRenderer.h
)

set(SOURCES
    ${SRC_DIR}/DefaultSceneCompiler.cpp
    ${SRC_DIR}/RenderBackendFactory.cpp
    ${SRC_DIR}/MinimalOpenGLBackend.cpp
    ${SRC_DIR}/RenderCoreRenderer.cpp
)

# 查找 Qt
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui OpenGL OpenGLWidgets REQUIRED)

# 创建静态库
add_library(${TARGET_NAME} STATIC
    ${HEADERS}
    ${SOURCES}
)

# 包含目录
target_include_directories(${TARGET_NAME} PUBLIC
    ${SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Utility/Utility/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Engine/Common/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Engine/2D/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Engine/3D/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Render/Common/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Render/2D/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Render/3D/Include
)

# 链接 Qt
target_link_libraries(${TARGET_NAME} PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::OpenGL
    Qt${QT_VERSION_MAJOR}::OpenGLWidgets
)

# C++17
target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)

# MSVC 编码
if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /utf-8 /FS)
endif()

# 设置属性
set_target_properties(${TARGET_NAME} PROPERTIES
    OUTPUT_NAME ${TARGET_NAME}
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
)

message(STATUS "[UiRenderCore]")
message(STATUS "  Type: Static library")
message(STATUS "  Sources: ${SOURCES}")
message(STATUS "")