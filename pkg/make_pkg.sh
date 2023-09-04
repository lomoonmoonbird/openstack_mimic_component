#!/bin/bash

function help(){
    echo 'Build neutron: git commit -m "build_mimic: xxxx"'
}

function build_mimic_rpm(){
    sudo bash ./rpm/x86_64/make_rpm.sh
}

function build_mimic_deb(){
    sudo bash ./deb/aarch64/make_deb.sh
}

pushd $(dirname $(realpath $0))
    if echo $CI_COMMIT_TITLE | grep "build_mimic"; then
        build_mimic_$PackageType
    else
        help
    fi
popd