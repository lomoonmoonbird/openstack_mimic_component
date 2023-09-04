#!/bin/bash

# set -e
CHECK_STATUS=0

PROXY_HORIZON_PORT=9773
PROXY_NOVA_PORT=8774
PROXY_CINDER_PORT=8776
PROXY_NEUTRON_PORT=9696
PROXY_KEYSTONE_PORT=5000

SCHEDULE_HORIZON_PORT=9099
SCHEDULE_CONTROLLER_PORT=8099
SCHEDULE_KEYSTONE_PORT=7099

SCHEDULE_HORIZON_CLIENT_PORT=809
SCHEDULE_CONTROLLER_CLIENT_PORT=709
SCHEDULE_KEYSTONE_CLIENT_PORT=609


JUDGE_HORIZON_PORT=9001
JUDGE_CONTROLLER_PORT=8001

RPC_PROXY_PORT1=10001
RPC_PROXY_PORT2=10002

CHECK_TYPE="ps"
RESTART_STATUS=0

function logger() {
    echo `date +"%Y-%m-%d %H:%M:%S:"` "$1" >> /var/log/check_mimic.log
}

function is_process_exist_lsof() {
    lsof -i:$1 2>&1 >/dev/null
	if [ $? -eq 0 ];then
		return
	else
		return 1
	fi
}

function is_process_exist_ps() {
    ps -ef | grep $1 2>&1 >/dev/null
	if [ $? -eq 0 ];then
		return
	else
		return 1
	fi
}

function check_service_lsof() {
    is_process_exist_lsof $1
    if [ $? -eq 0 ];then
        logger "$2, $1 is active"
		return
	else
        logger "$2, $1 is inactive"
        CHECK_STATUS=1
		return 1
	fi
}

function check_service_ps() {
    is_process_exist_ps $1
    if [ $? -eq 0 ];then
        logger "$1 is active"
		return
	else
        logger "$1 is inactive"
        CHECK_STATUS=1
		return 1
	fi
}

function check_losf() {
    logger "----check start------"
    check_service_lsof $PROXY_HORIZON_PORT "proxy_horizon"
    check_service_lsof $PROXY_NOVA_PORT "proxy_nova"
    check_service_lsof $PROXY_CINDER_PORT "proxy_cinder"
    check_service_lsof $PROXY_NEUTRON_PORT "proxy_neutron"
    check_service_lsof $PROXY_KEYSTONE_PORT "proxy_keystone"

    check_service_lsof $SCHEDULE_HORIZON_PORT "schedule_horizon"
    check_service_lsof $SCHEDULE_CONTROLLER_PORT "schedule_controller"
    check_service_lsof $SCHEDULE_KEYSTONE_PORT "schedule_keystone"

    #check_service $SCHEDULE_HORIZON_CLIENT_PORT "schedule_horizon_cli"
    #check_service $SCHEDULE_CONTROLLER_CLIENT_PORT "schedule_controller_cli"
    #check_service $SCHEDULE_KEYSTONE_CLIENT_PORT "schedule_keystone_cli"

    check_service_lsof $JUDGE_HORIZON_PORT "judge_horizon"
    check_service_lsof $JUDGE_CONTROLLER_PORT "judge_controller"

    check_service_lsof $RPC_PROXY_PORT1 "rpc proxy1"
    check_service_lsof $RPC_PROXY_PORT2 "rpc proxy2"

    exit $CHECK_STATUS
}

function check_ps() {
    logger "----check start------"
    check_service_ps "mimic_proxy_horizon"
    check_service_ps "mimic_proxy_nova"
    check_service_ps "mimic_proxy_cinder"
    check_service_ps "mimic_proxy_neutron"
    check_service_ps "mimic_proxy_keystone"

    check_service_ps "mimic_schedule_horizon"
    check_service_ps "mimic_schedule_controller"
    check_service_ps "mimic_schedule_keystone"

    #check_service $SCHEDULE_HORIZON_CLIENT_PORT "schedule_horizon_cli"
    #check_service $SCHEDULE_CONTROLLER_CLIENT_PORT "schedule_controller_cli"
    #check_service $SCHEDULE_KEYSTONE_CLIENT_PORT "schedule_keystone_cli"

    check_service_ps "mimic_judge_horizon"
    check_service_ps "mimic_judge_controller"

    #check_service_lsof $RPC_PROXY_PORT1 "rpc proxy1"
    #check_service_lsof $RPC_PROXY_PORT2 "rpc proxy2"
    supervisorctl status |grep RUNNING 2>&1 >/dev/null
    if [ $? -eq 0 ];then
        logger "rpc proxy is active"
		return
	else
        logger "rpc proxy is inactive"
        CHECK_STATUS=1
	fi

    if [ $CHECK_STATUS -eq 1 ];then
        if [ $RESTART_STATUS -eq 0 ];then
            logger "check fail, restart all service"
            RESTART_STATUS=1
            pkill redis; sleep 0.5;
            /usr/bin/redis-server  /etc/redis.conf
            supervisorctl stop all;sleep 0.5;supervisorctl start all
            systemctl restart mimic-cloud
            sleep 0.5
        fi
    fi

    exit $CHECK_STATUS
}

function notify_master() {
    logger "notfiy master>>>>>>>"
    /usr/bin/redis-server  /etc/redis.conf
    systemctl start mimic-cloud 2>&1 >/dev/null || logger "start maybe error"
    supervisorctl start all
}

function notify_slaver() {
    logger "notfiy slaver>>>>>>>"
    systemctl stop mimic-cloud 2>&1 >/dev/null || logger "stop maybe error"
    supervisorctl stop all
    pkill redis
}

function main() {
    case $1 in 
        "check")
            check_$CHECK_TYPE
        ;;
        "notify-master")
            notify_master
        ;;
        "notify-slaver")
            notify_slaver
        ;;
        *)
            logger "invalid input"
        ;;
    esac
}

main $@
