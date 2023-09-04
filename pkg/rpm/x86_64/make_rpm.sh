#!/bin/bash

RPM_BUILD_ROOT_PATH="/tmp/MCS-mimic"
rm -rf $RPM_BUILD_ROOT_PATH
mkdir -p $RPM_BUILD_ROOT_PATH/rpmbuild/SOURCES
mkdir -p $RPM_BUILD_ROOT_PATH/rpmbuild/SPECS
mkdir -p $RPM_BUILD_ROOT_PATH/rpmbuild/BUILD


pushd $(dirname $(realpath $0))
    scp -r ../../../* $RPM_BUILD_ROOT_PATH/rpmbuild/SOURCES/
    scp *.spec $RPM_BUILD_ROOT_PATH/rpmbuild/SPECS/
    docker login 192.168.66.29 -u admin -p comleader@123
    docker pull 192.168.66.29/package_build/mimic_rpm_build
    if [ $? -ne 0 ]; then
        docker build -t 192.168.66.29/package_build/mimic_rpm_build:latest .
        docker push 192.168.66.29/package_build/mimic_rpm_build:latest
    fi
popd


docker ps -a|grep mimic_rpm_build| awk '{print $1}'|xargs docker rm -f
docker run -v $RPM_BUILD_ROOT_PATH/rpmbuild:/root/rpmbuild:rw --name mimic_rpm_build 192.168.66.29/package_build/mimic_rpm_build:latest

sudo chown -R gitlab-runner $RPM_BUILD_ROOT_PATH/rpmbuild/RPMS/x86_64/*.rpm

echo "push to ftp"

sudo ftp -n<<!
open 192.168.67.183 4449
user chenkai comleader@123
binary
lcd $RPM_BUILD_ROOT_PATH/rpmbuild/RPMS/x86_64/
cd /packages/rpms/
put mimic_cloud-1.0-1.el7.x86_64.rpm
put spdlog-1.9.0-1.el7.x86_64.rpm
prompt
close
bye
!

rm -rf $RPM_BUILD_ROOT_PATH
