#!/bin/bash
#set -e

source ./config.sh
source ./variables.sh

function base_deploy() {
    yum -y install centos-release-scl centos-release-scl-rh
    yum -y install devtoolset-7
    yum -y install libevent-devel
    yum -y install mariadb-devel.x86_64
}

function set_service_enable() {
    hostname -I | grep $1 2>&1 >/dev/null
    if [ $? -eq 0 ]; then
        return 1
    fi
    return 0
}

function check_package_ok() {
    if [ $1 = "devtoolset-7" ]; then
        scl -l |grep devtoolset-7 2>&1 >/dev/null
        if [ $? -ne 0 ]; then
            echo -e "devtoolset-7 not installed"
            exit 1
        fi
    else 
        rpm -qa |grep $1 2>&1 >/dev/null
        if [ $? -ne 0 ]; then
            echo -e "$1 not installed"
            exit 1
        fi
    fi
}

function before_config_conf() {
    if [ $DEPLOY_TYPE != "compile" ] && [ $DEPLOY_TYPE != "rpm" ]; then
        echo -e "deploy type invalid"
    fi

    check_package_ok "devtoolset-7"
    check_package_ok "libevent-devel"
    check_package_ok "mariadb-devel"

    mkdir -p /etc/mimic-cloud/
    mkdir -p /home/deploy/deploy_mimic/

    grep "source /opt/rh/devtoolset-7/enable" /etc/profile \
    || echo "source /opt/rh/devtoolset-7/enable" >> /etc/profile

    source /etc/profile

    if [ $DEPLOY_TYPE = "compile" ]; then
        pushd ../
            tar -zxvf cmake-3.23.0-linux-x86_64.tar.gz
            mkdir -p ./build
            tar -zxvf spdlog-1.9.0.tar.gz
            mkdir -p ./spdlog-1.9.0/build
        popd

        pushd ../spdlog-1.9.0/build
            ../../cmake-3.23.0-linux-x86_64/bin/cmake .. -B .
            make -j20
            make install
        popd

        rm -rf ../build/*
        ../cmake-3.23.0-linux-x86_64/bin/cmake .. -B ../build
        pushd ../build
            make -j20
            make install 
        popd
    else
        check_package_ok "spdlog"
        check_package_ok "mimic"
    fi
    
    ldconfig

    ln -sf /usr/local/bin/mimic-cloud /usr/bin/mimic-mimic_cloud
    ln -sf /usr/local/lib/libyamlcpp.so /usr/lib64/libyamlcpp.so
    ln -sf /usr/local/lib/libjsoncpp.so /usr/lib64/libjsoncpp.so
    chmod 777 /usr/local/bin/mimic-cloud
    chmod 777 /opt/keystone_util.py
    chmod 777 -R /home/deploy/deploy_mimic/*
}

function start_mimic_services() {
    systemctl daemon-reload

    systemctl start mimic-cloud@schedule_horizon
    systemctl enable mimic-cloud@schedule_horizon

    systemctl start mimic-cloud@schedule_controller
    systemctl enable mimic-cloud@schedule_controller

    systemctl start mimic-cloud@schedule_keystone
    systemctl enable mimic-cloud@schedule_keystone

    systemctl start mimic-cloud@proxy_horizon
    systemctl enable mimic-cloud@proxy_horizon

    systemctl start mimic-cloud@proxy_nova
    systemctl enable mimic-cloud@proxy_nova

    systemctl start mimic-cloud@proxy_cinder
    systemctl enable mimic-cloud@proxy_cinder

    systemctl start mimic-cloud@proxy_neutron
    systemctl enable mimic-cloud@proxy_neutron

    systemctl start mimic-cloud@proxy_keystone
    systemctl enable mimic-cloud@proxy_keystone

    systemctl start mimic-cloud@judge_horizon
    systemctl enable mimic-cloud@judge_horizon

    systemctl start mimic-cloud@judge_controller
    systemctl enable mimic-cloud@judge_controller

    systemctl status mimic-cloud@*
}

function init_enable_vars() {
    set_service_enable $PROXY_HORIZON_NODE
    ENABLE_PROXY_HORIZON=$?
    set_service_enable $PROXY_NOVA_NODE
    ENABLE_PROXY_NOVA=$?
    set_service_enable $PROXY_KEYSTONE_NODE
    ENABLE_PROXY_KEYSTONE=$?
    set_service_enable $PROXY_CINDER_NODE
    ENABLE_PROXY_CINDRE=$?
    set_service_enable $PROXY_NEUTRON_NODE
    ENABLE_PROXY_NEUTRON=$?

    set_service_enable $JUDGE_HORIZON_NODE
    ENABLE_JUDGE_HORIZON=$?
    set_service_enable $JUDGE_CONTROLLER_NODE
    ENABLE_JUDGE_CONTROLLER=$?

    set_service_enable $SCHEDULE_HORIZON_NODE
    ENBALE_SCHEDULE_HORIZON=$?
    set_service_enable $SCHEDULE_CONTROLLER_NODE
    ENBALE_SCHEDULE_CONTROLLER=$?
    set_service_enable $SCHEDULE_KEYSTONE_NODE
    ENBALE_SCHEDULE_KEYSTONE=$?
}

function set_logcuter_to_crontab() {
    crontab -l |grep '/home/deploy/deploy_mimic/logcuter.sh proxy' \
     || echo '01 17 * * * /home/deploy/deploy_mimic/logcuter.sh proxy' >> /var/spool/cron/root

    crontab -l |grep '/home/deploy/deploy_mimic/logcuter.sh judge' \
     || echo '01 17 * * * /home/deploy/deploy_mimic/logcuter.sh judge' >> /var/spool/cron/root
    
    crontab -l |grep '/home/deploy/deploy_mimic/logcuter.sh schedule' \
     || echo '01 17 * * * /home/deploy/deploy_mimic/logcuter.sh schedule' >> /var/spool/cron/root
    
    crontab -l |grep '/home/deploy/deploy_mimic/logcuter.sh check_mimic' \
     || echo '01 17 * * * /home/deploy/deploy_mimic/logcuter.sh check_mimic' >> /var/spool/cron/root
}

function init_yml_config() {
    cat > /etc/mimic-cloud/mimic_cloud.yml<<EOF
global:
  exec_num: 3
  mysql:
    host: $CONTROLLER_IP
    port: 3306
    user: root
    passwd: comleader@123
    mysqldb: mimic
  schedule_horizon:
    ip: $SCHEDULE_HORIZON_NODE
    port: 9099
  schedule_controller:
    ip: $SCHEDULE_CONTROLLER_NODE
    port: 8099
  schedule_keystone:
    ip: $SCHEDULE_KEYSTONE_NODE
    port: 7099
schedule:
  - name: schedule_horizon
    enable: $ENBALE_SCHEDULE_HORIZON
    bind: 0.0.0.0
    port: 9099
    timeout: 10
    report_interval: 2
  - name: schedule_controller
    enable: $ENBALE_SCHEDULE_CONTROLLER
    bind: 0.0.0.0
    port: 8099
    timeout: 10
    report_interval: 2
  - name: schedule_keystone
    enable: $ENBALE_SCHEDULE_KEYSTONE
    bind: 0.0.0.0
    port: 7099
    timeout: 10
    report_interval: 2
proxy:
  - name: proxy_horizon
    enable: $ENABLE_PROXY_HORIZON
    bind: 0.0.0.0
    port: 9773
    work_threads: 5
    timeout: 25
  - name: proxy_nova
    enable: $ENABLE_PROXY_NOVA
    bind: 0.0.0.0
    port: 8774
    work_threads: 5
    timeout: 20
  - name: proxy_cinder
    enable: $ENABLE_PROXY_CINDRE
    bind: 0.0.0.0
    port: 8776
    work_threads: 5
    timeout: 20
  - name: proxy_neutron
    enable: $ENABLE_PROXY_NEUTRON
    bind: 0.0.0.0
    port: 9696
    work_threads: 5
    timeout: 20
  - name: proxy_keystone
    enable: $ENABLE_PROXY_KEYSTONE
    bind: 0.0.0.0
    port: 5000
    work_threads: 5
    timeout: 15
judge:
  - name: judge_horizon
    enable: $ENABLE_JUDGE_HORIZON
    bind: 0.0.0.0
    port: 9001
    work_threads: 5
    timeout: 20
  - name: judge_controller
    enable: $ENABLE_JUDGE_CONTROLLER
    bind: 0.0.0.0
    port: 8001
    work_threads: 5
    timeout: 15
EOF

# config horizon executors
    echo "exec-horizon:" >> /etc/mimic-cloud/mimic_cloud.yml
    while read -r line
    do
        ip=`echo $line |awk '{print $1}'`
        arch=`echo $line |awk '{print $2}'`
        cat >> /etc/mimic-cloud/mimic_cloud.yml<<EOF
  - ip: $ip
    port: 38888
    os: Centos
    web: nginx
    arch: $arch
  - ip: $ip
    port: 38889
    os: Ubuntu
    web: Apache
    arch: $arch
  - ip: $ip
    port: 38890
    os: Opensuse
    web: Zeus
    arch: $arch
EOF
    done < $EXECUTORS_CONFIG_FILE

# config controller executors
    echo "exec-controller:" >> /etc/mimic-cloud/mimic_cloud.yml
    while read -r line
    do
        ip=`echo $line |awk '{print $1}'`
        arch=`echo $line |awk '{print $2}'`
        cat >> /etc/mimic-cloud/mimic_cloud.yml<<EOF
  - ip: $ip
    port: 18774
    os: Centos
    web: nginx
    arch: $arch
  - ip: $ip
    port: 28774
    os: Opensuse
    web: Apache
    arch: $arch
  - ip: $ip
    port: 38774
    os: Ubuntu
    web: Zeus
    arch: $arch
EOF
    done < $EXECUTORS_CONFIG_FILE

# config keystone executors
    echo "exec-keystone:" >> /etc/mimic-cloud/mimic_cloud.yml
    while read -r line
    do
        ip=`echo $line |awk '{print $1}'`
        arch=`echo $line |awk '{print $2}'`
        cat >> /etc/mimic-cloud/mimic_cloud.yml<<EOF
  - ip: $ip
    port: 15000
    os: Centos
    web: nginx
    arch: $arch
  - ip: $ip
    port: 25000
    os: Ubuntu
    web: Apache
    arch: $arch
  - ip: $ip
    port: 35000
    os: Opensuse
    web: Zeus
    arch: $arch
EOF
    done < $EXECUTORS_CONFIG_FILE

cat >> /etc/mimic-cloud/mimic_cloud.yml<<EOF
not_support_key:
  - id
  - href
  - adminPass
  - updated
  - total_local_gb_usage
  - total_memory_mb_usage
  - stop
  - hours
  - total_vcpus_usage
  - _msg_id
  - _reply_q
  - total_hours
  - uptime
  - hostId
  - url
  - updated_at
  - scheduled_at
  - _context_request_id
  - _context_timestamp
  - _unique_id
  - attachment_id
  - updated_at
  - scheduled_at
  - nova_object.changes
  - task_state
  - revision_number
  - executor_ip
  - versioned_object.changes
  - _context_roles
  - allowed_address_pairs
  - vif_details
  - event_type
  - tags
  - network_metadata
  - network_info
  - subnets
  - audit_ids
  - security_groups
  - update_time
  - deleted_at
  - issued_at
  - heartbeat_timestamp
  - data->status
  - data->expiration_time
  - create_time
  - hypervisors->disk_available_least
  - data->open_time
  - data->sys_time
  - data->sys_timestamp
EOF
}