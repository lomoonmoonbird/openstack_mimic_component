CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
PROJECT(mimic-cloud) #项目名称

execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpfullversion -dumpversion OUTPUT_VARIABLE GCC_VERSION)
string(REGEX MATCHALL "[0-9]+" GCC_VERSION_COMPONENTS ${GCC_VERSION})
list(GET GCC_VERSION_COMPONENTS 0 GCC_MAJOR)
list(GET GCC_VERSION_COMPONENTS 1 GCC_MINOR)

set(GCC_VERSION "${GCC_MAJOR}.${GCC_MINOR}")
message(STATUS "gcc version=${GCC_VERSION}")

#set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m64 ")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m64 ")
add_definitions(-std=c++14)

#include_directories(
#${PROJECT_BINARY_DIR}/src/include
##${PROJECT_SOURCE_DIR}/src/inlcude
#${PROJECT_SOURCE_DIR}/src)
#SET(CMAKE_CXX_COMPILER "g++")
#add_definitions(-std=c++11)
#set(CMAKE_VERBOSE_MAKEFILE ON)


find_library(MYSQL_SO_PATH libmysqlclient.so /usr/lib /usr/lib64 /usr/lib64/mysql/)
if("${MYSQL_SO_PATH}" STREQUAL "MYSQL_SO_PATH-NOTFOUND")
  message("not found mysqlclient.so")
endif()

include_directories(
${PROJECT_SOURCE_DIR}/libs/include/
)
include_directories(
${PROJECT_SOURCE_DIR}/libs/include/evhtp/
)
include_directories(
${PROJECT_SOURCE_DIR}/src/include/
)
# include_directories(
# ${PROJECT_SOURCE_DIR}/src/include/public/
# )
include_directories(
${PROJECT_SOURCE_DIR}/libs/include/yaml-cpp/
)
include_directories(
${PROJECT_SOURCE_DIR}/libs/include/hiredis/
)
include_directories(
${PROJECT_SOURCE_DIR}/libs/include/redis++/
)
# include_directories(
# ${PROJECT_SOURCE_DIR}/src/libxml2/include/
# )
include_directories(
/usr/include/python2.7/
)
#include_directories(
#${PROJECT_SOURCE_DIR}/src/include/json/
#)

#把当前目录(.)下所有源代码文件和头文件加入变量SRC_LIST
AUX_SOURCE_DIRECTORY(. SRC_LIST)
#生成应用程序 hello (在windows下会自动生成hello.exe)
#ADD_EXECUTABLE(hello ${SRC_LIST})

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# add_subdirectory(src/public)
add_subdirectory(libs/yamlcpp)
add_subdirectory(libs/jsoncpp)
add_subdirectory(libs/evhtp)
add_subdirectory(libs/hiredis)
add_subdirectory(libs/redis++)
add_subdirectory(deploy)

# add_subdirectory(src/mimic_executor)
# add_subdirectory(src/proxy_task)
# add_subdirectory(src/judge_task)
# add_subdirectory(src/mimic_http_proxy)
# add_subdirectory(src/mimic_http_judge)

add_subdirectory(src)
# add_subdirectory(src/mimic_proxy)
# add_subdirectory(src/write_log)
#add_subdirectory(src/libxml2)
add_subdirectory(etc)
add_subdirectory(systemd)



add_dependencies(mimic-cloud yamlcpp jsoncpp evhtp hiredis redis++)
# add_dependencies(mimic_judge judge_task mimic_http_judge mimic_executor public yamlcpp jsoncpp evhtp hiredis redis++)
# add_dependencies(write_log public yamlcpp jsoncpp evhtp)


SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -rdynamic -O0 -Wall -g -ggdb ")
#set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_BUILD_TYPE "Release")

install(FILES keystone_util.py DESTINATION /opt/)
