#!/bin/bash

scl enable devtoolset-7 bash << EOF
pushd /root/rpmbuild/SPECS
    rpmbuild -ba spdlog.spec
    rpm -Uvh --force --nodeps ../RPMS/x86_64/spdlog-1.9.0-1.el7.x86_64.rpm
    rpmbuild -ba mimic-cloud.spec
popd
EOF
