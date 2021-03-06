cmake_minimum_required(VERSION 2.8.3)
project(packets_preprocessor)


find_package(OpenCV REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(YAML_CPP REQUIRED yaml-cpp)
find_path(YAML_CPP_INCLUDE_DIR
          NAMES yaml_cpp.h
          PATHS ${YAML_CPP_INCLUDE_DIRS})
find_library(YAML_CPP_LIBRARY
             NAMES YAML_CPP
             PATHS ${YAML_CPP_LIBRARY_DIRS})

link_directories(${YAML_CPP_LIBRARY_DIRS})

find_package(catkin REQUIRED COMPONENTS
    autoware_build_flags
    roscpp
    roslib
    sensor_msgs
    angles
    velodyne_msgs
    velodyne_pointcloud
    pcl_ros
    pcl_conversions
    cv_bridge
    image_transport
)

catkin_package(
    CATKIN_DEPENDS
    roscpp
    roslib
    sensor_msgs
    angles
    velodyne_msgs
    velodyne_pointcloud
    pcl_ros
    cv_bridge
    image_transport
#    INCLUDE_DIRS include
)

include_directories(
    ${catkin_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
)

add_executable(detect_invalid_near_points
  nodes/detect_invalid_near_points_node.cpp
  nodes/detect_invalid_near_points_core.cpp
  nodes/rawdata.cpp
  nodes/pointcloudXYZIRAD.cpp
)
target_link_libraries(detect_invalid_near_points
  ${catkin_LIBRARIES}
  ${OpenCV_LIBRARIES}
  ${YAML_CPP_LIBRARIES}
)
add_dependencies(detect_invalid_near_points
  ${catkin_EXPORTED_TARGETS}
)

# install(TARGETS detect_near_point
#         RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})
