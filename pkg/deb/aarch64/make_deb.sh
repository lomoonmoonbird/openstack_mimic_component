#!/bin/bash

RPM_BUILD_ROOT_PATH="/tmp/MCS-mimic"

rm -rf $RPM_BUILD_ROOT_PATH
mkdir -p $RPM_BUILD_ROOT_PATH/sources
pushd $(dirname $(realpath $0))
    scp -r ../../../* $RPM_BUILD_ROOT_PATH/sources/
    mkdir -p $RPM_BUILD_ROOT_PATH/sources/build/
popd

pushd $RPM_BUILD_ROOT_PATH/sources/
    echo "set(CPACK_PACKAGE_CONTACT \"dph@comleader.com.cn\")" >> CMakeLists.txt
    echo "include(CPack)" >> CMakeLists.txt
popd

docker login 192.168.66.29 -u admin -p comleader@123
docker pull 192.168.66.29/package_build/mimic_deb_build
if [ $? -ne 0 ]; then
    docker build -t 192.168.66.29/package_build/mimic_deb_build:latest .
    docker push 192.168.66.29/package_build/mimic_deb_build:latest
fi

docker ps -a|grep mimic_deb_build| awk '{print $1}'|xargs docker rm -f || true
docker run -v $RPM_BUILD_ROOT_PATH:/root/debbuild:rw --name mimic_deb_build 192.168.66.29/package_build/mimic_deb_build:latest

sudo chown -R gitlab-runner $RPM_BUILD_ROOT_PATH/sources/spdlog-1.9.0/build/*.deb
sudo chown -R gitlab-runner $RPM_BUILD_ROOT_PATH/sources/build/*.deb

echo "push to ftp"

ftp -n<<!
open 192.168.67.183 4449
user chenkai comleader@123
binary
lcd $RPM_BUILD_ROOT_PATH/sources/spdlog-1.9.0/build/
cd /packages/debs/
put spdlog-1.9.0.deb
lcd $RPM_BUILD_ROOT_PATH/sources/build/
cd /packages/debs/
put mimic-cloud-0.1.1-Linux.deb
prompt
close
bye
!

rm -rf $RPM_BUILD_ROOT_PATH


