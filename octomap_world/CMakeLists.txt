cmake_minimum_required(VERSION 2.8.3)
project(octomap_world)

find_package(catkin_simple REQUIRED)
catkin_simple(ALL_DEPS_REQUIRED)
find_package(octomap REQUIRED)

#find_package(octomap REQUIRED)
include("/usr/local/share/octomap/octomap-config.cmake")
message ("====================== ${OCTOMAP_LIBRARY_DIRS} ${OCTOMAP_INCLUDE_DIRS} ${OCTOMAP_LIBRARIES}")

add_definitions(-std=c++11)
include_directories(${OCTOMAP_INCLUDE_DIRS})

#############
# LIBRARIES #
#############
cs_add_library(${PROJECT_NAME}
  src/octomap_world.cc
  src/octomap_manager.cc
)

############
# BINARIES #
############
add_executable(octomap_manager
    src/octomap_manager_node.cc
  )
target_link_libraries(octomap_manager ${OCTOMAP_LIBRARIES} ${PROJECT_NAME})

##########
# EXPORT #
##########
cs_install()
cs_export()
