project(ROS)

set(INC_DIR ./include)
set(SRC_DIR ./src)
set(BUILD_DIR ./build)
set(OBJ_DIR ${BUILD_DIR}/objs)

include_directories( ${INC_DIR} )

set(SOURCE_FILES ${SRC_DIR}/*)
set(SOURCE_FILES ${SRC_DIR}/filesystem/*)
