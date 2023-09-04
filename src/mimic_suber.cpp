#include <stdlib.h>

#include "mimic_suber.hpp"
#include "mimic_struct.hpp"
#include "mimic_schedule.hpp"
#include "json/json.h"

MimicSuber::MimicSuber(MimicCommon *common, MimicService *service, struct event_base* base):common(common),
service(service), base(base) {
    this->sername = service->baseInfo->serviceName;
    this->logger = service->logger;
    initBoolFlag();
    this->mutexInSuber = PTHREAD_MUTEX_INITIALIZER;
    judgeRule = "majority,weight,time";
    judgeErrorCount = "5";
    scheduleRule = "host_random";
    scheduleInterval = "60";

    isInit = true;
}

MimicSuber::~MimicSuber(){}

void MimicSuber::initBoolFlag()
{
    isHorizon = false;
    isController = false;
    isKeystone = false;
    isSchedule = false;
    isProxy = false;
    isNova =false;
    isCinder = false;
    isNeutron =false;
    shouldBeConvert = false;
    string::size_type position;

    position = sername.find("schedule");
    if (position != string::npos) isSchedule = true;

    position = sername.find("proxy");
    if (position != string::npos) isProxy = true;

    position = sername.find("horizon");
    if (position != string::npos) isHorizon = true;

    bool isCtr = false;
    position = sername.find("nova");
    if (position != string::npos) isNova = true;

    position = sername.find("cinder");
    if (position != string::npos) isCinder = true;

    position = sername.find("neutron");
    if (position != string::npos) isNeutron = true;

    position = sername.find("controller");
    if (position != string::npos) isCtr = true;

    isController = isNova || isCinder || isNeutron || isCtr;

    shouldBeConvert = (isProxy && isCinder) || (isProxy && isNeutron);

    position = sername.find("keystone");
    if (position != string::npos) isKeystone = true;
}

int MimicSuber::convertToCincerOrNeutron(int sport) {
    if(shouldBeConvert) {
        if(isCinder) {
            return MimicUtil::novaToCinder(sport);
        } else if(isNeutron) {
            return MimicUtil::novaToNeutron(sport);
        } else {
            return sport;
        }
    } else {
        return sport;
    }
}

int MimicSuber::fillExecMap(Json::Value& value)
{
    logger->debug("fill map value size: {}", value.size());
    if(value.size()==0) return 0;
    pthread_mutex_lock(&mutexInSuber);
    allExecList.clear();
    activeExecList.clear();
    for (int i = 0; i < value.size(); i++) {
        executor_t executorInfo = {
            value[i]["id"].asInt(),
            value[i]["status"].asInt(),
            value[i]["exec_zone"].asInt(),
            value[i]["system_num"].asString(),
            value[i]["arch"].asString(),
            value[i]["note"].asString(),
            value[i]["cpu"].asString(),
            value[i]["ip"].asString(),
            isProxy ? convertToCincerOrNeutron(value[i]["port"].asInt()) : value[i]["port"].asInt(),
            value[i]["web"].isNull() ? "" : value[i]["web"].asString(),
            value[i]["os"].asString(),
            value[i]["scweight"].asInt(),
            value[i]["active_time"].asUInt64(),
            value[i]["failcount"].asInt()
        };
        string ip_port = executorInfo.ip + ":" + std::to_string(executorInfo.port);
        if(value[i]["status"].asInt()==1) {
            activeExecList.insert(pair<string, executor_t>(ip_port, executorInfo));
            allExecList.insert(pair<string, executor_t>(ip_port, executorInfo));
        } else {
            allExecList.insert(pair<string, executor_t>(ip_port, executorInfo));
        }
    }
    pthread_mutex_unlock(&mutexInSuber);
    logger->debug("active size: {}, all size: {}", activeExecList.size(), allExecList.size());

    return 0;
}

int MimicSuber::setPolicys(Json::Value& value)
{
    // bool restartTimer = false;
    pthread_mutex_lock(&mutexInSuber);
    if(this->judgeRule != value["judge_rule"].asString())
        this->judgeRule = value["judge_rule"].asString();

    if(this->judgeErrorCount != value["judge_err_count"].asString())
        this->judgeErrorCount = value["judge_err_count"].asString();

    if(this->scheduleRule != value["schedule_rule"].asString())
        this->scheduleRule = value["schedule_rule"].asString();

    if(this->scheduleInterval != value["schedule_interval"].asString()) {
        this->scheduleInterval = value["schedule_interval"].asString();
        // restartTimer = true;
    }
    if(this->mimicSwitch != value["mimic_switch"].asString()) {
        this->mimicSwitch = value["mimic_switch"].asString();
    }
    pthread_mutex_unlock(&mutexInSuber);
    //调度不进行自己订阅自己
    // if(restartTimer && isSchedule) {
    //     MimicSchedule *scheduleInstance = dynamic_cast<MimicSchedule*>(this->service);
    //     scheduleInstance->autoScheduleTimer->timerStop();
    //     scheduleInstance->autoScheduleTimer->second = atoi(scheduleInterval.c_str());
    //     scheduleInstance->autoScheduleTimer->timerSpawn();
    // }
    return 0;
}

void MimicSuber::readCb(struct bufferevent *bev, void *arg) 
{
    MimicSuber *that = (MimicSuber*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    logger->debug("data come, read cb");

    while (true) {
        struct evbuffer *input = bufferevent_get_input(bev);
        size_t sz = evbuffer_get_length(input);
        logger->info("size receive: {}", sz);

        char *line = evbuffer_readln(input, NULL, EVBUFFER_EOL_CRLF);
        if(line == NULL || strlen(line) == 0) {
            // if(line != NULL || strlen(line) == 0) free(line);
            bufferevent_flush(bev, EV_READ, BEV_NORMAL);
            break;
        }
        string str = line;
        free(line);
        logger->debug("Receive line suber: {}", str);
        Json::Value value;
        Json::Reader reader;
        try {
            reader.parse(str, value);
        } catch (...) {
            logger->error("invalid req data, sync mysql");
            that->syncExecFromMysql();
            that->syncPolicyFromMysql();
            bufferevent_flush(bev, EV_READ, BEV_NORMAL);
            break;
        }
        try {
            that->fillExecMap(value["all_exec_list"]);
            that->setPolicys(value);
        } catch(...) {
            logger->error("json data formt error, sync mysql");
        }
    }
    logger->info("read cb end");
}
 
void MimicSuber::writeCb(struct bufferevent *bev, void *arg)
{
    MimicSuber *that = (MimicSuber*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    struct evbuffer *output = bufferevent_get_output(bev);
	if (evbuffer_get_length(output) == 0) {
        logger->debug("Write ok");
	} else {
		logger->debug("In writing");
    }
}
 
void MimicSuber::eventCb(struct bufferevent *bev, short events, void *arg)
{
    MimicSuber *that = (MimicSuber*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    if (events & BEV_EVENT_EOF) {
        logger->error("connection closed, resubscribe");  
        bufferevent_free(bev);
        usleep(5000000);
        that->subscribe();
    } else if(events & BEV_EVENT_ERROR) {
        logger->error("some other error, resubscribe");
        bufferevent_free(bev);
        usleep(5000000);
        that->subscribe();
    } else if(events & BEV_EVENT_CONNECTED) {
        logger->info("suber connected server");
        Json::Value value;
        Json::FastWriter jsonwriter;
        value["mode"] = "subscribe";
        string str = jsonwriter.write(value);
        // str += "\r\n";
        char endbuf[2] = {'\r', '\n'};
        logger->debug("write to buf, subscribe, {}", str);
        bufferevent_write(bev, str.c_str(), strlen(str.c_str()));
        bufferevent_write(bev, endbuf, 2);
        bufferevent_flush(bev, EV_WRITE, BEV_NORMAL);
    }

    //长连接不设置超时时间
    // else if(events & BEV_EVENT_TIMEOUT) {
    //     logger->error("Timout read or wirte");
    //     bufferevent_free(bev);
    //     usleep(500000);
    //     that->subscribe();
    // }
}

int MimicSuber::subscribe() 
{
    logger->debug("Subscribe ev start");
    syncPolicyFromMysql();
    if(isSchedule) {
        logger->debug("is schedule dont subscribe");
        return 0;
    }
     
    int fd = socket(AF_INET, SOCK_STREAM, 0);
 
    struct bufferevent* bev = NULL;
    bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
 
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;

    string sc_ip;
    int sc_port;
    if (isHorizon) {
        sc_ip = this->service->globalConf->schedule_horizon_ip;
        sc_port = this->service->globalConf->schedule_horizon_port;
    } else if(isController) {
        sc_ip = this->service->globalConf->schedule_controller_ip;
        sc_port = this->service->globalConf->schedule_controller_port;
    } else if (isKeystone) {
        sc_ip = this->service->globalConf->schedule_keystone_ip;
        sc_port = this->service->globalConf->schedule_keystone_port;
    }
    logger->info("sc ip: {}, sc_port: {}", sc_ip, sc_port);
    serv.sin_port = htons(sc_port);
    inet_pton(AF_INET, sc_ip.c_str(), &serv.sin_addr.s_addr);
 
    bufferevent_socket_connect(bev, (struct sockaddr*)&serv, sizeof(serv));
    bufferevent_setcb(bev, MimicSuber::readCb, MimicSuber::writeCb, MimicSuber::eventCb, this);
    bufferevent_enable(bev, EV_READ);
    bufferevent_enable(bev, EV_WRITE);
    //长连接不设置超时时间
    //bufferevent_set_timeouts(bev, &(this->service->rTimeout), &(this->service->wTimeout));

    return 0;
}

void MimicSuber::syncExecFromMysql()
{
    logger->debug("Sync mysql execs start");
    string execStatusTable;
    if(isHorizon) execStatusTable = "exec_status_info";
    if(isController) execStatusTable = "exec_status_info_n";
    if(isKeystone) execStatusTable = "exec_status_info_k";
    if(!execStatusTable.empty()) {
        string sql = "SELECT * FROM " + execStatusTable;
        MysqlExecRes_t res;
        common->execSql(res, sql);
        if(!res.isSuccess) {
            if(!allExecList.empty() && !activeExecList.empty()) {
                return;
            } else {
                pthread_mutex_lock(&mutexInSuber);
                map<string, int> tmpiplist;
                int id = 0;
                for(auto item : service->execInfo) {
                    id += 1;
                    executor_t executorInfo = {
                        id,
                        0,
                        id,
                        "exec_"+ std::to_string(id),
                        item.arch,
                        "-",
                        item.arch,
                        item.ip,
                        isProxy ? convertToCincerOrNeutron(item.port): item.port,
                        item.web,
                        item.os,
                        100,
                        0,
                        0
                    };
                    string ip_port = executorInfo.ip + ":" + std::to_string(executorInfo.port);
                    if(tmpiplist.find(item.ip) == tmpiplist.end()) {
                        executorInfo.status = 1;
                        activeExecList.insert(pair<string, executor_t>(ip_port, executorInfo));
                        allExecList.insert(pair<string, executor_t>(ip_port, executorInfo));
                    } else {
                        allExecList.insert(pair<string, executor_t>(ip_port, executorInfo));
                    }
                }
                pthread_mutex_unlock(&mutexInSuber);
                return;
            }
        }
        pthread_mutex_lock(&mutexInSuber);
        map<string, executor_t> tmpallexecList = std::move(allExecList);
        allExecList.clear();
        activeExecList.clear();
        for(auto itouter = res.result.begin(); itouter!=res.result.end(); itouter++) {
            string keyaddr = (*itouter)["ip"] + ":" + (*itouter)["port"];
            executor_t executorInfo = {
                atoi((*itouter)["id"].c_str()),
                atoi((*itouter)["status"].c_str()),
                atoi((*itouter)["exec_zone"].c_str()),
                (*itouter)["system_num"],
                (*itouter)["arch"],
                (*itouter)["note"],
                (*itouter)["cpu"],
                (*itouter)["ip"],
                isProxy ? convertToCincerOrNeutron(atoi((*itouter)["port"].c_str())) : atoi((*itouter)["port"].c_str()),
                (*itouter)["web"].empty() ? "" : (*itouter)["web"],
                (*itouter)["os"],
                atoi((*itouter)["scweight"].c_str()),
                tmpallexecList.find(keyaddr) == tmpallexecList.end() ? 0 : tmpallexecList[keyaddr].activeTime,
                tmpallexecList.find(keyaddr) == tmpallexecList.end() ? 0 : tmpallexecList[keyaddr].failCount
            };
            string ip_port = executorInfo.ip + ":" + std::to_string(executorInfo.port);
            if(atoi((*itouter)["status"].c_str())==1) {
                activeExecList.insert(pair<string, executor_t>(ip_port, executorInfo));
                allExecList.insert(pair<string, executor_t>(ip_port, executorInfo));
            } else {
                allExecList.insert(pair<string, executor_t>(ip_port, executorInfo));
            }
        }
        pthread_mutex_unlock(&mutexInSuber);
    }
}

void MimicSuber::syncPolicyFromMysql()
{
    logger->debug("Sync mysql policy start");
    string sql = "SELECT `conf_key`, `conf_value` FROM `configurable_mimic_config`;";
    MysqlExecRes_t res;
    common->execSql(res, sql);
    if (!res.isSuccess) {
        return;
    }
    pthread_mutex_lock(&mutexInSuber);
    for(auto itouter = res.result.begin(); itouter!=res.result.end(); itouter++) {
        auto itinnerKey = (*itouter).begin();
        auto itinnerVal = (*itouter).rbegin();
        if(isHorizon) {
            if ((*itinnerKey).second == "judge_rule_h") {
                if (judgeRule != (*itinnerVal).second) 
                    judgeRule = (*itinnerVal).second;
            }
            if ((*itinnerKey).second == "judge_err_counts_h") {
                if(judgeErrorCount != (*itinnerVal).second) 
                    judgeErrorCount = (*itinnerVal).second;
            }
            if ((*itinnerKey).second == "timer_schedule_interval_h") {
                if(scheduleInterval != (*itinnerVal).second) {
                    if(!scheduleInterval.empty() && isSchedule) {
                        scheduleInterval = (*itinnerVal).second;
                        if(!isInit) {
                            logger->info("Restart auto schedule timer, in sync mysql");
                            MimicSchedule *scheduleInstance = dynamic_cast<MimicSchedule*>(service);
                            scheduleInstance->autoScheduleTimer->timerStop();
                            scheduleInstance->autoScheduleTimer->second = atoi(scheduleInterval.c_str());
                            scheduleInstance->autoScheduleTimer->timerSpawn();
                        }
                    } else {
                        scheduleInterval = (*itinnerVal).second;
                    }
                }
            }
            if ((*itinnerKey).second == "schedule_rule_h") {
                if(scheduleRule != (*itinnerVal).second)
                    scheduleRule = (*itinnerVal).second;
            }
            //仅供前端查询使用，不进行发布
            // if ((*itinnerKey).second == "schedule_rule_select_h") {
            //     if(scheduleRule != (*itinnerVal).second)
            //         schedulePolicySelect = (*itinnerVal).second;
            // }
            if ((*itinnerKey).second == "mimic_switch") {
                if(mimicSwitch != (*itinnerVal).second)
                    mimicSwitch = (*itinnerVal).second;
            }
        }
        if(isController) {
            if ((*itinnerKey).second == "judge_rule_n") {
                if (judgeRule != (*itinnerVal).second) 
                    judgeRule = (*itinnerVal).second;
            }
            if ((*itinnerKey).second == "judge_err_counts_n") {
                if(judgeErrorCount != (*itinnerVal).second) 
                    judgeErrorCount = (*itinnerVal).second;
            }
            if ((*itinnerKey).second == "timer_schedule_interval_n") {
                if(scheduleInterval != (*itinnerVal).second) {
                    if(!scheduleInterval.empty() && isSchedule) {
                        scheduleInterval = (*itinnerVal).second;
                        if(!isInit) {
                            logger->info("Restart auto schedule timer, in sync mysql");
                            MimicSchedule *scheduleInstance = dynamic_cast<MimicSchedule*>(service);
                            scheduleInstance->autoScheduleTimer->timerStop();
                            scheduleInstance->autoScheduleTimer->second = atoi(scheduleInterval.c_str());
                            scheduleInstance->autoScheduleTimer->timerSpawn();
                        }
                            
                    } else {
                        scheduleInterval = (*itinnerVal).second;
                    }
                }
            }
            if ((*itinnerKey).second == "schedule_rule_n") {
                if(scheduleRule != (*itinnerVal).second)
                    scheduleRule = (*itinnerVal).second;
            }
            //仅供前端查询使用，不进行发布
            // if ((*itinnerKey).second == "schedule_rule_select_n") {
            //     if(scheduleRule != (*itinnerVal).second)
            //         schedulePolicySelect = (*itinnerVal).second;
            // }
            if ((*itinnerKey).second == "mimic_switch") {
                if(mimicSwitch != (*itinnerVal).second)
                    mimicSwitch = (*itinnerVal).second;
            }
        }
        if(isKeystone) {
            if ((*itinnerKey).second == "judge_rule_k") {
                if (judgeRule != (*itinnerVal).second) 
                    judgeRule = (*itinnerVal).second;
            }
            if ((*itinnerKey).second == "judge_err_counts_k") {
                if(judgeErrorCount != (*itinnerVal).second) 
                    judgeErrorCount = (*itinnerVal).second;
            }
            if ((*itinnerKey).second == "timer_schedule_interval_k") {
                if(scheduleInterval != (*itinnerVal).second) {
                    if(!scheduleInterval.empty() && isSchedule) {
                        scheduleInterval = (*itinnerVal).second;
                        if(!isInit) {
                            logger->info("Restart auto schedule timer, in sync mysql");
                            MimicSchedule *scheduleInstance = dynamic_cast<MimicSchedule*>(service);
                            scheduleInstance->autoScheduleTimer->timerStop();
                            scheduleInstance->autoScheduleTimer->second = atoi(scheduleInterval.c_str());
                            scheduleInstance->autoScheduleTimer->timerSpawn();
                        }
                    } else {
                        scheduleInterval = (*itinnerVal).second;
                    }
                }
            }
            if ((*itinnerKey).second == "schedule_rule_k") {
                if(scheduleRule != (*itinnerVal).second)
                    scheduleRule = (*itinnerVal).second;
            }
            //仅供前端查询使用，不进行发布
            // if ((*itinnerKey).second == "schedule_rule_select_k") {
            //     if(scheduleRule != (*itinnerVal).second)
            //         schedulePolicySelect = (*itinnerVal).second;
            // }
            if ((*itinnerKey).second == "mimic_switch") {
                if(mimicSwitch != (*itinnerVal).second)
                    mimicSwitch = (*itinnerVal).second;
            }
        }
    }
    isInit = false;
    pthread_mutex_unlock(&mutexInSuber);
}
