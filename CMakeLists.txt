cmake_minimum_required(VERSION 3.15)
project(usvc_comm CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# boost 库是静态运行库(mt-s)，项目统一 /MT 匹配
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

## 源码是 UTF-8，统一告诉 MSVC，避免中文被当 GBK 读出的 C4819 警告
if(MSVC)
  add_compile_options(/utf-8)
endif()

# 关 boost 自动链接，避免去找不存在的 vc143 库名
add_compile_definitions(BOOST_ALL_NO_LIB)

# 库是静态库 + 静态运行库(mt-s)，让 boost config 接受这个 variant
set(Boost_USE_STATIC_RUNTIME ON)
set(Boost_USE_STATIC_LIBS    ON)

# 指到 boost 自带的 config 目录（关键！Boost_DIR 必须单独一行，不能被注释吞掉）
set(Boost_DIR "C:/Users/PC/Desktop/win_usvc/third_rely/boost_1_80_0/stage/lib/cmake/Boost-1.80.0" CACHE PATH "" FORCE)
find_package(Boost 1.80 CONFIG REQUIRED COMPONENTS filesystem)

## 可执行：usvc_comm（测试 Upper / Ship 的单进程小主程序）
add_executable(usvc_comm
  main.cpp
  upper_node.cpp
  ../config/Config.cpp
  ../config_manager_tool/config_manager_tool.cpp
  MTLink/MTLink.c
  MTLink/MTLink_Core.c
)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}            # upper_node.h / MTLink/
  ${CMAKE_CURRENT_SOURCE_DIR}/MTLink     # MTLink_DataStruct.h（SharedState 裸名 include）
  ${CMAKE_CURRENT_SOURCE_DIR}/..         # 让 "config/Config.h"、"config_manager_tool/..." 可解析
  ${CMAKE_CURRENT_SOURCE_DIR}/../common  # SharedState.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../usvc_navi        # NavData.h 所在根（SharedState 用）
  ${CMAKE_CURRENT_SOURCE_DIR}/../usvc_navi/NavData # NavData.h 实际在 NavData 子目录
)

## MTLink 的 C 文件按 C 语言编译
set_source_files_properties(MTLink/MTLink.c PROPERTIES LANGUAGE C)
set_source_files_properties(MTLink/MTLink_Core.c PROPERTIES LANGUAGE C)

## Windows 需要链接 Winsock + Boost 库
if(WIN32)
  target_link_libraries(usvc_comm PRIVATE ws2_32 Boost::filesystem)
endif()

target_compile_definitions(usvc_comm PRIVATE
  _WIN32_WINNT=0x0601
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)