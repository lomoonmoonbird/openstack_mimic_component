#!/bin/bash
pushd /home/deploy/deploy_mimic
source /home/deploy/deploy_mimic/config.sh

mysql -h$CONTROLLER_IP -P3306 -uroot -p'comleader@123' mimic -e "DELETE FROM exec_log_info WHERE DATE(create_time) <= DATE(DATE_SUB(NOW(),INTERVAL 7 DAY))"
mysql -h$CONTROLLER_IP -P3306 -uroot -p'comleader@123' mimic -e "DELETE FROM exec_log_info_n WHERE DATE(create_time) <= DATE(DATE_SUB(NOW(),INTERVAL 7 DAY))"
mysql -h$CONTROLLER_IP -P3306 -uroot -p'comleader@123' mimic -e "DELETE FROM exec_log_info_k WHERE DATE(create_time) <= DATE(DATE_SUB(NOW(),INTERVAL 7 DAY))"
mysql -h$CONTROLLER_IP -P3306 -uroot -p'comleader@123' mimic -e "DELETE FROM judge_log_info WHERE DATE(create_time) <= DATE(DATE_SUB(NOW(),INTERVAL 7 DAY))"
mysql -h$CONTROLLER_IP -P3306 -uroot -p'comleader@123' mimic -e "DELETE FROM judge_log_info_n WHERE DATE(create_time) <= DATE(DATE_SUB(NOW(),INTERVAL 7 DAY))"
mysql -h$CONTROLLER_IP -P3306 -uroot -p'comleader@123' mimic -e "DELETE FROM judge_log_info_k WHERE DATE(create_time) <= DATE(DATE_SUB(NOW(),INTERVAL 7 DAY))"

cd /var/log/mimic-cloud/

case "$1" in
        "proxy" )
        #保存代理日志
        if [ -e /var/log/mimic-cloud/proxy_horizon.log ]; then
                cp /var/log/mimic-cloud/proxy_horizon.log `date "+%Y-%m-%d"`_proxy_horizon.log
                tar czvf `date "+%Y-%m-%d"`_proxy_horizon_log.tar.gz `date "+%Y-%m-%d"_proxy_horizon.log`
                mv `date "+%Y-%m-%d"`_proxy_horizon_log.tar.gz /tmp/
                rm `date "+%Y-%m-%d"`_proxy_horizon.log -f
                echo > /var/log/mimic-cloud/proxy_horizon.log
                find /tmp -mtime +1  | grep _proxy_horizon_log.tar.gz | xargs rm -f
        fi

        if [ -e /var/log/mimic-cloud/proxy_nova.log ]; then
                cp /var/log/mimic-cloud/proxy_nova.log `date "+%Y-%m-%d"`_proxy_nova.log
                tar czvf `date "+%Y-%m-%d"`_proxy_nova_log.tar.gz `date "+%Y-%m-%d"_proxy_nova.log`
                mv `date "+%Y-%m-%d"`_proxy_nova_log.tar.gz /tmp/
                rm `date "+%Y-%m-%d"`_proxy_nova.log -f
                echo > /var/log/mimic-cloud/proxy_nova.log
                find /tmp -mtime +1  | grep _proxy_nova_log.tar.gz | xargs rm -f
        fi

        if [ -e /var/log/mimic-cloud/proxy_cinder.log ]; then
                cp /var/log/mimic-cloud/proxy_cinder.log `date "+%Y-%m-%d"`_proxy_cinder.log
                tar czvf `date "+%Y-%m-%d"`_proxy_cinder_log.tar.gz `date "+%Y-%m-%d"_proxy_cinder.log`
                mv `date "+%Y-%m-%d"`_proxy_cinder_log.tar.gz /tmp/
                rm `date "+%Y-%m-%d"`_proxy_cinder.log -f
                echo > /var/log/mimic-cloud/proxy_cinder.log
                find /tmp -mtime +1  | grep _proxy_cinder_log.tar.gz | xargs rm -f
        fi

        if [ -e /var/log/mimic-cloud/proxy_neutron.log ]; then
                cp /var/log/mimic-cloud/proxy_neutron.log `date "+%Y-%m-%d"`_proxy_neutron.log
                tar czvf `date "+%Y-%m-%d"`_proxy_neutron_log.tar.gz `date "+%Y-%m-%d"_proxy_neutron.log`
                mv `date "+%Y-%m-%d"`_proxy_neutron_log.tar.gz /tmp/
                rm `date "+%Y-%m-%d"`_proxy_neutron.log -f
                echo > /var/log/mimic-cloud/proxy_neutron.log
                find /tmp -mtime +1  | grep _proxy_neutron_log.tar.gz | xargs rm -f
        fi

        if [ -e /var/log/mimic-cloud/proxy_keystone.log ]; then
                cp /var/log/mimic-cloud/proxy_keystone.log `date "+%Y-%m-%d"`_proxy_keystone.log
                tar czvf `date "+%Y-%m-%d"`_proxy_keystone_log.tar.gz `date "+%Y-%m-%d"_proxy_keystone.log`
                mv `date "+%Y-%m-%d"`_proxy_keystone_log.tar.gz /tmp/
                rm `date "+%Y-%m-%d"`_proxy_keystone.log -f
                echo > /var/log/mimic-cloud/proxy_keystone.log
                find /tmp -mtime +1  | grep _proxy_keystone_log.tar.gz | xargs rm -f
        fi
        ;;
        "judge" )
        #保存裁决日志
        if [ -e /var/log/mimic-cloud/judge_horizon.log ] ; then
                cp /var/log/mimic-cloud/judge_horizon.log `date "+%Y-%m-%d"`_judge_horizon.log
                tar czvf `date "+%Y-%m-%d"`_judge_horizon_log.tar.gz `date "+%Y-%m-%d"_judge_horizon.log`
                mv `date "+%Y-%m-%d"`_judge_horizon_log.tar.gz /tmp/
                rm `date "+%Y-%m-%d"`_judge_horizon.log -f
                echo > /var/log/mimic-cloud/judge_horizon.log
                find /tmp -mtime +1  | grep _judge_horizon_log.tar.gz | xargs rm -f
        fi

        if [ -e /var/log/mimic-cloud/judge_controller.log ] ; then
                cp /var/log/mimic-cloud/judge_controller.log `date "+%Y-%m-%d"`_judge_controller.log
                tar czvf `date "+%Y-%m-%d"`_judge_controller_log.tar.gz `date "+%Y-%m-%d"_judge_controller.log`
                mv `date "+%Y-%m-%d"`_judge_controller_log.tar.gz /tmp/
                rm `date "+%Y-%m-%d"`_judge_controller.log -f
                echo > /var/log/mimic-cloud/judge_controller.log
                find /tmp -mtime +1  | grep _judge_controller_log.tar.gz | xargs rm -f
        fi
        ;;
        "schedule" )
        #保存调度日志
        if [ -e /var/log/mimic-cloud/schedule_horizon.log ] ; then
                cp /var/log/mimic-cloud/schedule_horizon.log `date "+%Y-%m-%d"`_schedule_horizon.log
                tar czvf `date "+%Y-%m-%d"`_schedule_horizon_log.tar.gz `date "+%Y-%m-%d"_schedule_horizon.log`
                mv `date "+%Y-%m-%d"`_schedule_horizon_log.tar.gz /tmp/
                rm `date "+%Y-%m-%d"`_schedule_horizon.log -f
                echo > /var/log/mimic-cloud/schedule_horizon.log
        fi

        if [ -e /var/log/mimic-cloud/schedule_controller.log ] ; then
                cp /var/log/mimic-cloud/schedule_controller.log `date "+%Y-%m-%d"`_schedule_controller.log
                tar czvf `date "+%Y-%m-%d"`_schedule_controller_log.tar.gz `date "+%Y-%m-%d"_schedule_controller.log`
                mv `date "+%Y-%m-%d"`_schedule_controller_log.tar.gz /tmp/
                rm `date "+%Y-%m-%d"`_schedule_controller.log -f
                echo > /var/log/mimic-cloud/schedule_controller.log
        fi

        if [ -e /var/log/mimic-cloud/schedule_keystone.log ] ; then
                cp /var/log/mimic-cloud/schedule_keystone.log `date "+%Y-%m-%d"`_schedule_keystone.log
                tar czvf `date "+%Y-%m-%d"`_schedule_keystone_log.tar.gz `date "+%Y-%m-%d"_schedule_keystone.log`
                mv `date "+%Y-%m-%d"`_schedule_keystone_log.tar.gz /tmp/
                rm `date "+%Y-%m-%d"`_schedule_keystone.log -f
                echo > /var/log/mimic-cloud/schedule_keystone.log
        fi
        ;;
        "check_mimic" )
        #保存裁决日志
        pushd /var/log/
        if [ -e /var/log/check_mimic.log ] ; then
                cp /var/log/check_mimic.log `date "+%Y-%m-%d"`_check_mimic.log
                tar czvf `date "+%Y-%m-%d"`_check_mimic_log.tar.gz `date "+%Y-%m-%d"_check_mimic.log`
                mv `date "+%Y-%m-%d"`_check_mimic_log.tar.gz /tmp/
                rm `date "+%Y-%m-%d"`_check_mimic.log -f
                echo > /var/log/check_mimic.log
                find /tmp -mtime +1  | grep _check_mimic_log.tar.gz | xargs rm -f
        fi
        popd
        ;;
esac
popd
