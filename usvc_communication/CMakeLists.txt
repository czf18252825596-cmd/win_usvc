cmake_minimum_required(VERSION 3.10)
project(usvc_navi)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## 源码是 UTF-8，统一告诉 MSVC，避免中文被当 GBK 读出的 C4819 警告
if(MSVC)
  add_compile_options(/utf-8)
endif()

## 可执行：usvc_navi_node
add_executable(usvc_navi
  src/usvc_navi.cpp
  config/Config.cpp
  src/UDP/udp.cpp
  src/ULink/ULink.c
  src/ULink/ULink_Core.c
)

include_directories(
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}          # config/、NavData/、Common/
  ${CMAKE_CURRENT_SOURCE_DIR}/src      # UDP/、ULink/
)
)

## ULink 的 C 文件按 C 语言编译
set_source_files_properties(src/ULink/ULink.c PROPERTIES LANGUAGE C)
set_source_files_properties(src/ULink/ULink_Core.c PROPERTIES LANGUAGE C)

## Windows 需要链接 Winsock 库
if(WIN32)
  target_link_libraries(usvc_navi ws2_32)
endif()