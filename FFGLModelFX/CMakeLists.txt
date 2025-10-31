cmake_minimum_required(VERSION 3.20)
project(FFGLModelFX LANGUAGES C CXX)

# 选项：构建类型
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS: 使用 arm64（与 Homebrew assimp 同架构），避免链接架构不匹配
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for macOS" FORCE)

include(FetchContent)

# FFGL SDK 路径（可选）
set(FFGL_SDK_DIR "" CACHE PATH "Path to local FFGL 2.x SDK root (optional, else fetch from Git)")
set(FFGL_GIT_TAG "master" CACHE STRING "FFGL Git ref to fetch when FFGL_SDK_DIR is empty (e.g. master or v2.2)")

# macOS bundle 设置
set(BUNDLE_NAME "ModelFXFFGL")
set(BUNDLE_ID   "com.yt.ffgl.modelfx")

# 源码
set(SRC
  src/FFGLModelFX.cpp
  src/GLShader.cpp
  src/OBJLoader.cpp
  src/FBXLoader.cpp
)

set(HEADERS
  src/FFGLModelFX.h
  src/GLShader.h
  src/OBJLoader.h
  src/FBXLoader.h
)

# 着色器资源（安装/拷贝进 bundle）
set(SHADERS
  resources/shaders/model.vert
  resources/shaders/model.frag
)

# 生成用于复制的绝对路径列表（构建步骤运行于构建目录）
set(SHADERS_ABS)
foreach(_s IN LISTS SHADERS)
  list(APPEND SHADERS_ABS "${CMAKE_CURRENT_SOURCE_DIR}/${_s}")
endforeach()

# 内置示例模型
set(MODEL_OBJ "${CMAKE_CURRENT_SOURCE_DIR}/resources/model.obj")

# 生成 bundle 目标（macOS）
add_library(${BUNDLE_NAME} MODULE ${SRC} ${HEADERS} ${SHADERS})
set_target_properties(${BUNDLE_NAME} PROPERTIES
  BUNDLE TRUE
  BUNDLE_EXTENSION bundle
  OUTPUT_NAME ${BUNDLE_NAME}
)

# 生成 Info.plist（适配非 Xcode 生成器）
set(BUNDLE_EXECUTABLE ${BUNDLE_NAME})
set(BUNDLE_IDENTIFIER ${BUNDLE_ID})
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in
  ${CMAKE_CURRENT_BINARY_DIR}/${BUNDLE_NAME}-Info.plist
  @ONLY
)
set_target_properties(${BUNDLE_NAME} PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/${BUNDLE_NAME}-Info.plist
)

# 引入 FFGL SDK：优先使用本地 FFGL_SDK_DIR；否则从 GitHub 拉取
if(EXISTS "${FFGL_SDK_DIR}")
  set(FFGL_ROOT ${FFGL_SDK_DIR})
else()
  message(STATUS "Fetching Resolume FFGL SDK from GitHub (tag=${FFGL_GIT_TAG})...")
  FetchContent_Declare(
    resolume_ffgl
    GIT_REPOSITORY https://github.com/resolume/ffgl.git
    GIT_TAG        ${FFGL_GIT_TAG}
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(resolume_ffgl)
  set(FFGL_ROOT ${resolume_ffgl_SOURCE_DIR})
endif()

# 基于 FFGL 源码组织一个最小可用的静态库以便链接（FFGLSDK + ffglex + quickstart）
set(FFGLSDK_SOURCES
  ${FFGL_ROOT}/source/lib/FFGLSDK.cpp
)
file(GLOB FFGLEX_SOURCES ${FFGL_ROOT}/source/lib/ffglex/*.cpp)
file(GLOB FFGLQS_SOURCES ${FFGL_ROOT}/source/lib/ffglquickstart/*.cpp)
add_library(ffglsdk STATIC ${FFGLSDK_SOURCES} ${FFGLEX_SOURCES} ${FFGLQS_SOURCES})
target_include_directories(ffglsdk PUBLIC
  ${FFGL_ROOT}/source/lib
  ${FFGL_ROOT}/source/lib/ffglex
  ${FFGL_ROOT}/source/lib/ffgl
  ${FFGL_ROOT}/source/lib/ffglquickstart
)

target_link_libraries(${BUNDLE_NAME} PRIVATE ffglsdk)
# 可选：集成 Assimp 以支持 FBX/多格式导入
option(USE_ASSIMP "Enable Assimp for FBX import" ON)
if(USE_ASSIMP)
  # 优先使用系统已安装的 assimp（Homebrew）
  find_package(assimp CONFIG QUIET)
  if(NOT assimp_FOUND)
    find_package(Assimp QUIET)
  endif()

  if(assimp_FOUND)
    message(STATUS "Using system assimp (config): ${assimp_DIR}")
    target_link_libraries(${BUNDLE_NAME} PRIVATE assimp::assimp)
    target_compile_definitions(${BUNDLE_NAME} PRIVATE USE_ASSIMP)
  elseif(Assimp_FOUND)
    message(STATUS "Using system Assimp (module): ${ASSIMP_LIBRARY}")
    target_include_directories(${BUNDLE_NAME} PRIVATE ${ASSIMP_INCLUDE_DIRS})
    target_link_libraries(${BUNDLE_NAME} PRIVATE ${ASSIMP_LIBRARIES})
    target_compile_definitions(${BUNDLE_NAME} PRIVATE USE_ASSIMP)
  else()
    message(STATUS "System assimp not found, fetching via FetchContent...")
    FetchContent_Declare(
      assimp
      GIT_REPOSITORY https://github.com/assimp/assimp.git
      GIT_TAG        v5.3.1
      GIT_SHALLOW    TRUE
      CMAKE_ARGS
        -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF
        -DASSIMP_BUILD_FBX_IMPORTER=ON
        -DASSIMP_NO_EXPORT=ON
        -DASSIMP_BUILD_TESTS=OFF
        -DASSIMP_BUILD_ZLIB=OFF
        -DSYSTEM_ZLIB=ON
        -DASSIMP_INSTALL=OFF
        -DASSIMP_WARNINGS_AS_ERRORS=OFF
    )
    # 仍保留 cache 变量以兼容某些 CMake 环境缓存复用
    set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "")
    set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "")
    set(ASSIMP_NO_EXPORT ON CACHE BOOL "")
    set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "")
    set(ASSIMP_BUILD_ZLIB OFF CACHE BOOL "")
    set(SYSTEM_ZLIB ON CACHE BOOL "")
    set(ASSIMP_INSTALL OFF CACHE BOOL "")
    FetchContent_MakeAvailable(assimp)
    target_link_libraries(${BUNDLE_NAME} PRIVATE assimp)
    target_compile_definitions(${BUNDLE_NAME} PRIVATE USE_ASSIMP)
  endif()
endif()

# macOS 下 OpenGL
find_library(COCOA_LIB Cocoa)
find_library(OPENGL_LIB OpenGL)
if(OPENGL_LIB)
  target_link_libraries(${BUNDLE_NAME} PRIVATE ${OPENGL_LIB} ${COCOA_LIB})
endif()

# 资源拷贝到 bundle（仅复制资源）
add_custom_command(TARGET ${BUNDLE_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_BUNDLE_CONTENT_DIR:${BUNDLE_NAME}>/Resources/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${SHADERS_ABS}
    "$<TARGET_BUNDLE_CONTENT_DIR:${BUNDLE_NAME}>/Resources/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${MODEL_OBJ}
    "$<TARGET_BUNDLE_CONTENT_DIR:${BUNDLE_NAME}>/Resources/model.obj"
  COMMENT "Copy shaders into bundle Resources"
)

# 可选：部署到 Resolume 的“Extra Effects”目录（在构建系统级别定义，不能嵌入在 add_custom_command 中）
option(DEPLOY_TO_RESOLUME "Copy bundle to Resolume Extra Effects after build" OFF)
set(RESOLUME_EXTRA_EFFECTS_DIR "$ENV{HOME}/Documents/Resolume/Extra Effects" CACHE PATH "Resolume Extra Effects directory")

# 手动部署命令：cmake --build <build> --target deploy
add_custom_target(deploy
  COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOLUME_EXTRA_EFFECTS_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:${BUNDLE_NAME}>" "${RESOLUME_EXTRA_EFFECTS_DIR}/${BUNDLE_NAME}.bundle"
  DEPENDS ${BUNDLE_NAME}
  COMMENT "Deploy ${BUNDLE_NAME}.bundle to ${RESOLUME_EXTRA_EFFECTS_DIR}"
)

# 自动部署（可选）：构建完成后自动复制到 Resolume 目录
if(DEPLOY_TO_RESOLUME)
  add_custom_command(TARGET ${BUNDLE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOLUME_EXTRA_EFFECTS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:${BUNDLE_NAME}>" "${RESOLUME_EXTRA_EFFECTS_DIR}/${BUNDLE_NAME}.bundle"
    COMMENT "Auto-deploy ${BUNDLE_NAME}.bundle to ${RESOLUME_EXTRA_EFFECTS_DIR}"
  )
endif()

# 编译定义（可选）
target_compile_definitions(${BUNDLE_NAME} PRIVATE
  BUNDLE_IDENTIFIER="${BUNDLE_ID}"
)
