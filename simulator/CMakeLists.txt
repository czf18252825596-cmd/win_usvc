cmake_minimum_required(VERSION 3.10)
project(usvc_navi_sim)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/utf-8)
endif()

## 发送端模拟器：复用 usvc_navi 的 ULink + UDP，保证帧格式与接收端一致
add_executable(usvc_navi_sim
  simulator.cpp
  ../usvc_navi/src/UDP/udp.cpp
  ../usvc_navi/src/ULink/ULink.c
  ../usvc_navi/src/ULink/ULink_Core.c
)

include_directories(
  ../usvc_navi/src
  ../usvc_navi/src/UDP
  ../usvc_navi/src/ULink
)

set_source_files_properties(../usvc_navi/src/ULink/ULink.c PROPERTIES LANGUAGE C)
set_source_files_properties(../usvc_navi/src/ULink/ULink_Core.c PROPERTIES LANGUAGE C)

if(WIN32)
  target_link_libraries(usvc_navi_sim ws2_32)
endif()