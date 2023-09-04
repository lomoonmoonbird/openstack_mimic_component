#!/bin/bash

#deploy type: rpm/compile
DEPLOY_TYPE="rpm"

PROXY_HORIZON_NODE=""
PROXY_NOVA_NODE=""
PROXY_KEYSTONE_NODE=""
PROXY_CINDER_NODE=""
PROXY_NEUTRON_NODE=""

JUDGE_HORIZON_NODE=""
JUDGE_CONTROLLER_NODE=""

SCHEDULE_HORIZON_NODE=""
SCHEDULE_CONTROLLER_NODE=""
SCHEDULE_KEYSTONE_NODE=""

#if deploy separate from deploy system,
#u shold create a file to be executors config file.
#The file content like folow:
#192.168.232.104   hygon_x86_64
#192.168.232.105   intel_x86_64
#192.168.226.45    kp_aarch64
#192.168.232.111   test_x86_32
EXECUTORS_CONFIG_FILE=""

CONTROLLER_IP=""