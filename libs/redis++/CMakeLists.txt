CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
PROJECT(redis++) #项目名称
#把当前目录(.)下所有源代码文件和头文件加入变量SRC_LIST


#set(REDIS_PLUS_PLUS_DEFAULT_CXX_STANDARD 17)
#if(NOT REDIS_PLUS_PLUS_CXX_STANDARD)
#    set(REDIS_PLUS_PLUS_CXX_STANDARD ${REDIS_PLUS_PLUS_DEFAULT_CXX_STANDARD} CACHE STRING "Set CXX standard" FORCE)
#    set_property(CACHE REDIS_PLUS_PLUS_CXX_STANDARD PROPERTY STRINGS "11" "14" "17" "20")
#endif()
message(STATUS "redis-plus-plus build with CXX standard: c++${REDIS_PLUS_PLUS_CXX_STANDARD}")

# cxx utils
set(HIREDIS_DIR "${PROJECT_SOURCE_DIR}/../../libs/include/hiredis")
set(CXX_UTILS_DIR "${PROJECT_SOURCE_DIR}/../../libs/include/redis++/cxx11")
set(TLS_SUB_DIR"${PROJECT_SOURCE_DIR}/../../libs/include/redis++/tls")
set(REDIS_PLUS_PLUS_ASYNC_FUTURE_HEADER"${PROJECT_SOURCE_DIR}/../../libs/include/redis++/future/std")





include_directories(
${HIREDIS_DIR}
${CXX_UTILS_DIR}
${TLS_SUB_DIR}
${REDIS_PLUS_PLUS_ASYNC_FUTURE_HEADER}
)
message(STATUS "CXX_UTILS_DIR:${CXX_UTILS_DIR}")

#add_compile_options(-std=c99)

AUX_SOURCE_DIRECTORY(. SRC_LIST)
#生成应用程序 hello (在windows下会自动生成hello.exe)
#add_library(redis++ SHARED  ${SRC_LIST})
add_library(redis++ ${SRC_LIST})
#add_library(public ${SRC_LIST})
