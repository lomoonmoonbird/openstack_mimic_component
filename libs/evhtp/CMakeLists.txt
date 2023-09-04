CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
PROJECT(evhtp) #项目名称
#把当前目录(.)下所有源代码文件和头文件加入变量SRC_LIST
set(EVHTP_DISABLE_REGEX ON)
# Test 32/64 bits
if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    add_definitions(-DEVHTP_SYS_ARCH=64)                                                                                                                                                                                             
elseif("${CMAKE_SIZEOF_VOID_P}" EQUAL "4")
    add_definitions(-DEVHTP_SYS_ARCH=32)
else()
    message(ERROR "Unable to determine architecture")
endif()

configure_file(
    ${PROJECT_SOURCE_DIR}/../../libs/include/evhtp/config.h.in
		${PROJECT_SOURCE_DIR}/../../libs/include/evhtp/config.h)

if (NOT HAVE_SYS_TREE)
    configure_file (
        ${PROJECT_SOURCE_DIR}/compat/sys/tree.h.in
        ${PROJECT_BINARY_DIR}/compat/sys/tree.h)

    include_directories(${PROJECT_BINARY_DIR}/compat)
endif()

if (NOT HAVE_SYS_QUEUE)
    configure_file (
        ${PROJECT_SOURCE_DIR}/compat/sys/queue.h.in
        ${PROJECT_BINARY_DIR}/compat/sys/queue.h)
    include_directories(${PROJECT_BINARY_DIR}/compat)
endif()






AUX_SOURCE_DIRECTORY(. SRC_LIST)
#生成应用程序 hello (在windows下会自动生成hello.exe)
#add_library(evhtp SHARED  ${SRC_LIST})
add_library(evhtp ${SRC_LIST})
