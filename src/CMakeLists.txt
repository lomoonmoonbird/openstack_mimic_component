CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
PROJECT(mimic-cloud) #项目名称
#把当前目录(.)下所有源代码文件和头文件加入变量SRC_LIST
AUX_SOURCE_DIRECTORY(. SRC_LIST)
#生成应用程序 hello (在windows下会自动生成hello.exe)
ADD_EXECUTABLE(mimic-cloud ${SRC_LIST})
target_link_libraries(mimic-cloud ${CMAKE_BINARY_DIR}/lib/libhiredis.a)  
target_link_libraries(mimic-cloud ${CMAKE_BINARY_DIR}/lib/libredis++.a)
target_link_libraries(mimic-cloud ${CMAKE_BINARY_DIR}/lib/libevhtp.a)  
target_link_libraries(mimic-cloud ${CMAKE_BINARY_DIR}/lib/libjsoncpp.so)  
target_link_libraries(mimic-cloud ${CMAKE_BINARY_DIR}/lib/libyamlcpp.so)  

#-levent -levent_openssl -levent_pthreads -lssl -lcrypto -levhtp -ldl -lrt
target_link_libraries(mimic-cloud event)  
target_link_libraries(mimic-cloud event_openssl)  
target_link_libraries(mimic-cloud event_pthreads)  
target_link_libraries(mimic-cloud ssl)  
target_link_libraries(mimic-cloud pthread)  
target_link_libraries(mimic-cloud crypto) 
target_link_libraries(mimic-cloud ${MYSQL_SO_PATH})

install(TARGETS mimic-cloud DESTINATION bin)
