#!/bin/bash
#set -e
source ./functions.sh

echo -e "deploy start"
#base_deploy
before_config_conf
init_enable_vars
init_yml_config
start_mimic_services
set_logcuter_to_crontab
echo -e "deploy end"