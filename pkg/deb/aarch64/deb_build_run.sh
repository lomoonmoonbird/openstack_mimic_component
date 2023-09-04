#!/bin/bash

tar -zxvf spdlog-1.9.0.tar.gz
pushd spdlog-1.9.0
    mkdir build
    pushd build
        cmake ..
        make -j20
        cpack -G DEB
        dpkg -i ./*.deb
    popd
popd

pushd ./build
    cmake ..
    make -j20
    cpack -G DEB
    dpkg -i ./*.deb
popd
