CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
PROJECT(yamlcpp) #项目名称
#把当前目录(.)下所有源代码文件和头文件加入变量SRC_LIST
AUX_SOURCE_DIRECTORY(. SRC_LIST)
#生成应用程序 hello (在windows下会自动生成hello.exe)
add_library(yamlcpp SHARED ${SRC_LIST})
#add_library(public ${SRC_LIST})

install(TARGETS yamlcpp DESTINATION lib)
