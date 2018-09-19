cmake_minimum_required(VERSION 2.8.3)
project(octomap_world)

find_package(catkin_simple REQUIRED)
catkin_simple(ALL_DEPS_REQUIRED)
find_package(octomap REQUIRED)

#find_package(octomap REQUIRED)
include("/usr/local/share/octomap/octomap-config.cmake")

#set(OCTOMAP_LIBRARIES_DIRS "/home/kuri/catkin_ws/src/octomap/lib")
#set(OCTOMAP_INCLUDE_DIRS "/home/kuri/catkin_ws/src/octomap/octomap/include")
# Set library names as absolute paths:
#set(OCTOMAP_LIBRARIES
#"/home/ku/catkin_ws/src/octomap/lib/liboctomap.so"
#"/home/ku/catkin_ws/src/octomap/lib/liboctomath.so"
#)



add_definitions(-std=c++11)
#include_directories(${OCTOMAP_INCLUDE_DIRS})


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
cs_add_executable(octomap_manager
  src/octomap_manager_node.cc
)
target_link_libraries(octomap_manager ${OCTOMAP_LIBRARIES} ${PROJECT_NAME})
##########
# EXPORT #
##########
cs_install()
cs_export()





#find_library(OCTOMAP_LIBRARY
#  NAMES octomap
#  PATHS /home/kuri/catkin_ws/src/octomap/lib
#)
#find_library(OCTOMATH_LIBRARY
#  NAMES octomath
#  PATHS /home/kuri/catkin_ws/src/octomap/lib
#)
#find_path(OCTOMAP_INCLUDE_DIR
#  NAMES "octomap/octomap.h"
#  PATHS /home/kuri/catkin_ws/src/octomap/octomap/include
#)
#set(OCTOMAP_INCLUDE_DIRS "${OCTOMAP_INCLUDE_DIR}")
#set(OCTOMAP_LIBRARIES "${OCTOMAP_LIBRARY};${OCTOMATH_LIBRARY}" )
