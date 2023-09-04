CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
PROJECT(evhtp) #项目名称
#把当前目录(.)下所有源代码文件和头文件加入变量SRC_LIST




add_compile_options(-std=c99)

AUX_SOURCE_DIRECTORY(. SRC_LIST)
#生成应用程序 hello (在windows下会自动生成hello.exe)
#add_library(hiredis SHARED  ${SRC_LIST})
add_library(hiredis ${SRC_LIST})
#add_library(public ${SRC_LIST})
