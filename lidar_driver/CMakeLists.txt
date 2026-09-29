cmake_minimum_required(VERSION 3.20)
project(lidar_driver)

# ---------- C++ 标准 ----------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---------- 寻找 PCL (排雷：去掉了 COMPONENTS common，加载全家桶) ----------
set(PCL_DIR "D:/PCL 1.15.1/cmake")
find_package(PCL 1.15 REQUIRED)

set(USVC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ---------- 目标：静态库 ----------
add_library(lidar_driver STATIC
    lidar_driver.cpp
    lidar_driver.h
)

# ---------- 包含头文件 (你的“偷懒”写法全靠这里) ----------
target_include_directories(lidar_driver PUBLIC
    ${USVC_ROOT}                                  # 找 lidar_driver.h
    ${USVC_ROOT}/usvc_communication/MTLink        # 找 MTLink
    ${USVC_ROOT}/common                           # 找 SharedState.h
    ${USVC_ROOT}/usvc_navi/NavData                # 找 NavData
    ${USVC_ROOT}/third_rely/eigen                 # 找 Eigen/Dense
    ${USVC_ROOT}/Livox-SDK2/include    # 找 livox_lidar_api.h
    ${PCL_INCLUDE_DIRS}                           # 找 pcl/*.h
)

# ---------- 链接库目录 ----------
target_link_directories(lidar_driver PUBLIC
    ${USVC_ROOT}/Livox-SDK2/build/sdk_core/Release
    ${PCL_LIBRARY_DIRS}
)

# ---------- 链接具体库文件 ----------
target_link_libraries(lidar_driver PUBLIC
    livox_lidar_sdk_static                      
    ${PCL_LIBRARIES}
)

# ---------- 注入 PCL 的宏定义 ----------
target_compile_definitions(lidar_driver PUBLIC ${PCL_DEFINITIONS})

add_executable(lidar_test lidar_test.cpp)

target_link_libraries(lidar_test PUBLIC
    lidar_driver
)