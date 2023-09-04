#include <iostream>
#include <sys/time.h>
#include <csignal>
#include <sys/prctl.h>
#include <stdlib.h>
#include <algorithm>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/http.h>
#include <event2/dns.h>
#include <set>

#include "mimic_suber.hpp"
#include "mimic_schedule.hpp"
#include "mimic_service.hpp"
#include "json/json.h"

MimicSchedule::MimicSchedule(GlobalInfo_t *globalConf, ServiceBaseInfo_t *baseInfo)
{
    initBaseInfo(globalConf, baseInfo);
    this->common = nullptr;
    this->suber = nullptr;
    this->autoReportTimer = nullptr;
    this->autoScheduleTimer = nullptr;

    this->isFirstStart = true;
    this->isHorizon = false;
    this->isController = false;
    this->isKeystone = false;
    string::size_type position;
    position = baseInfo->serviceName.find("horizon");
    if (position != string::npos) {
        isHorizon = true;
        execStatusInfoTb = "exec_status_info";
        execLogInfoTb = "exec_log_info";
    }
    position = baseInfo->serviceName.find("controller");
    if (position != string::npos) {
        isController = true;
        execStatusInfoTb = "exec_status_info_n";
        execLogInfoTb = "exec_log_info_n";
    }
    position = baseInfo->serviceName.find("keystone");
    if (position != string::npos) {
        isKeystone = true;
        execStatusInfoTb = "exec_status_info_k";
        execLogInfoTb = "exec_log_info_k";
    }

    if (isHorizon) testUrl = "/active/";
    else if(isController) testUrl = "/v2.1";
    else if(isKeystone) testUrl = "/v3";
    this->mutexExecs = PTHREAD_MUTEX_INITIALIZER;

    if(isHorizon) {
        portToExecOs.emplace(38888, "centos");
        portToExecOs.emplace(38889, "ubuntu");
        portToExecOs.emplace(38890, "redhat");
    } else if (isController) {
        portToExecOs.emplace(18774, "centos");
        portToExecOs.emplace(28774, "opensuse");
        portToExecOs.emplace(38774, "ubuntu");
    } else if(isKeystone) {
        portToExecOs.emplace(15000, "centos");
        portToExecOs.emplace(25000, "ubuntu");
        portToExecOs.emplace(35000, "opensuse");
    }

    // isCleaning = false;
    manualDownNum = 0;
}

MimicSchedule::~MimicSchedule() {
    if(common != nullptr){
        delete common;
        common = nullptr;
    }
    if(suber != nullptr){
        delete suber;
        suber = nullptr;
    }
    if(autoReportTimer != nullptr) {
        delete autoReportTimer;
        autoReportTimer = nullptr;
    }
    if(autoScheduleTimer != nullptr) {
        delete autoScheduleTimer;
        autoScheduleTimer = nullptr;
    }
}

bool MimicSchedule::initSubber()
{
    logger->debug("Suber init start");
    common = new MimicCommon(this->globalConf, this->logger);
    try{
        common->initMysql();
    } catch (const char* &e) {
        logger->error("Mysql Init fail: {}", e);
        // return false;
    }
    // logger->debug("Mysql init success");

    suber = new MimicSuber(common, dynamic_cast<MimicService*>(this), this->evbase);
    suber->subscribe();
    int count = 0;
    while (true) {   
        if((!suber->scheduleRule.empty())&& 
        (!suber->scheduleInterval.empty())) break;
        usleep(1000);
        count += 1;
        if (count > 5*1000) {
            logger->error("Suber thread init timeout");
            return false;
        }
    }
    logger->debug("Suber init success");
    return true;
}

void MimicSchedule::sigIntCbFun(int sig, short why, void *arg)
{
    MimicSchedule *that = (MimicSchedule*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    logger->debug("Stop service!");
    that->autoReportTimer->timerStop();
    that->autoScheduleTimer->timerStop();
    event_base_loopexit(that->evbase, NULL);
}

void MimicSchedule::syncExecLog(string addr, string relatedAddr, string action, string status, string msg) {
    Json::Value actionDetail;
    actionDetail["current_exec"] = addr;
    actionDetail["related_exec"] = relatedAddr;
    actionDetail["action"] = action;
    actionDetail["status"] = status;
    actionDetail["msg"] = msg;
    string errordetail = Json::FastWriter().write(actionDetail);
    if (!errordetail.empty() && errordetail.back() == '\n') {
        errordetail.pop_back();
    }
    string sqlinsert = "INSERT INTO %s SET exec_id = \"%s\", arch = \"%s\", os = \"%s\", ";
    sqlinsert += "web = \"%s\", action = \"%s\", action_detail = \'%s\', create_time = %s";
    char buff[2048] = {0};
    sprintf(buff, 
    sqlinsert.c_str(), 
    this->execLogInfoTb.c_str(), 
    std::to_string(this->suber->allExecList[addr].id).c_str(), 
    this->suber->allExecList[addr].arch.c_str(),
    this->suber->allExecList[addr].os.c_str(), 
    this->suber->allExecList[addr].web.c_str(), 
    (action + "," + status).c_str(), errordetail.c_str(), "now()");
    MysqlExecRes_t ret;
    this->common->execSql(ret, string(buff));
    if(!ret.isSuccess) logger->error("insert exec log error: {}", buff);
}

void MimicSchedule::updateExecStatusDb(string ip, int port, int status) {
    logger->debug("insert to db status:{}", status);
    int statusnew = -2;
    if(status == 2 || status == -2) {
        statusnew = -1;
    } else {
        statusnew = status;
    }
    logger->debug("status new: {}", statusnew);
    string sqlupdate = "UPDATE %s SET status = %d, update_time = %s WHERE ip = \"%s\" AND port = \"%s\";";
    char buff[5096] = {0};
    sprintf(buff, sqlupdate.c_str(), this->execStatusInfoTb.c_str(),
    statusnew, "now()", ip.c_str(), std::to_string(port).c_str());
    MysqlExecRes_t ret;
    this->common->execSql(ret, string(buff));
    if(!ret.isSuccess) logger->error("update exec status error: {}", buff);
}

void MimicSchedule::syncAndPub(struct bufferevent *bev) {
    if(isFirstStart) {
        logger->warn("not init finish");
        return;
    }
    Json::Value outer;
    Json::Value outerall;
    for (auto &item : this->suber->allExecList) {
        Json::Value innerall;
        innerall["id"] = item.second.id;
        innerall["status"] = item.second.status;
        innerall["exec_zone"] = item.second.execZone;
        innerall["system_num"] = item.second.systemNum;
        innerall["arch"] = item.second.arch;
        innerall["note"] = item.second.note;
        innerall["cpu"] = item.second.cpu;
        innerall["ip"] = item.second.ip;
        innerall["port"] = item.second.port;
        innerall["web"] = item.second.web;
        innerall["os"] = item.second.os;
        innerall["scweight"] = item.second.scweight;

        innerall["active_time"] = item.second.activeTime;
        innerall["failcount"] = item.second.failCount;
        outerall.append(innerall);
    }
    outer["all_exec_list"] = outerall;
    
    Json::Value outeractive;
    for (auto &item : this->suber->activeExecList) {
        Json::Value inneractive;
        inneractive["id"] = item.second.id;
        inneractive["status"] = item.second.status;
        inneractive["exec_zone"] = item.second.execZone;
        inneractive["system_num"] = item.second.systemNum;
        inneractive["arch"] = item.second.arch;
        inneractive["note"] = item.second.note;
        inneractive["cpu"] = item.second.cpu;
        inneractive["ip"] = item.second.ip;
        inneractive["port"] = item.second.port;
        inneractive["web"] = item.second.web;
        inneractive["os"] = item.second.os;
        inneractive["scweight"] = item.second.scweight;

        inneractive["active_time"] = item.second.activeTime;
        inneractive["failcount"] = item.second.failCount;
        outeractive.append(inneractive);
    }
    outer["active_exec_list"] = outeractive;
    outer["judge_rule"] = this->suber->judgeRule;
    outer["judge_err_count"] = this->suber->judgeErrorCount;
    outer["schedule_rule"] = this->suber->scheduleRule;
    outer["schedule_interval"] = this->suber->scheduleInterval;
    outer["mimic_switch"] = this->suber->mimicSwitch;
    if(outer.empty()) {
        logger->error("json value is empty, pub error");
        return;
    }

    Json::FastWriter jsonwriter;
    string str = jsonwriter.write(outer);
    // str += "\r\n";
    char endbuf[2] = {'\r', '\n'};
    logger->info("pub to subscribe: jsonall len: {}, jsonactive len: {}, subs size: {}", outerall.size(), outeractive.size(), subscribeBevs.size());
    if(bev == NULL) {
        for (auto itsub = subscribeBevs.begin(); itsub != subscribeBevs.end(); itsub++) {
            bufferevent_write(*itsub, str.c_str(), strlen(str.c_str()));
            bufferevent_write(*itsub, endbuf, 2);
            bufferevent_flush(*itsub, EV_WRITE, BEV_NORMAL);
        }
    } else {
        bufferevent_write(bev, str.c_str(), strlen(str.c_str()));
        bufferevent_write(bev, endbuf, 2);
        bufferevent_flush(bev, EV_WRITE, BEV_NORMAL);
    }
}

bool MimicSchedule::initMysqlAndPub() {
    Json::FastWriter jsonwriter;
    Json::Value outer;

    string sqlsel = "SELECT count(*) FROM " + this->execStatusInfoTb;
    MysqlExecRes_t ret;
    this->common->execSql(ret, string(sqlsel));
    if (!ret.isSuccess) {
        return false;
    }
    if(ret.result.begin()->begin()->second != "0") {
        string sqldel = "DELETE FROM " + this->execStatusInfoTb;
        MysqlExecRes_t ret;
        this->common->execSql(ret, string(sqldel));
        if(!ret.isSuccess) {
            logger->error("delete old data error");
            return false;
        }
    }
    Json::Value outerall;
    for (auto &item : this->suber->allExecList) {
        Json::Value innerall;
        innerall["id"] = item.second.id;
        innerall["status"] = item.second.status;
        innerall["exec_zone"] = item.second.execZone;
        innerall["system_num"] = item.second.systemNum;
        innerall["arch"] = item.second.arch;
        innerall["note"] = item.second.note;
        innerall["cpu"] = item.second.cpu;
        innerall["ip"] = item.second.ip;
        innerall["port"] = item.second.port;
        innerall["web"] = item.second.web.empty()?"":item.second.web;
        innerall["os"] = item.second.os;
        innerall["scweight"] = item.second.scweight;

        innerall["active_time"] = item.second.activeTime;
        innerall["failcount"] = item.second.failCount;
        outerall.append(innerall);

        char buff[5096] = {0};
        string sql = "INSERT INTO %s (id, status, exec_zone, system_num, arch, note, ";
        sql += "create_time, update_time, cpu, ip, port, web, os, scweight) VALUES ";
        sql += "(%d, %d, %d, \"%s\", \"%s\", \"%s\", %s, %s, \"%s\", \"%s\", %d, \"%s\", \"%s\", %d);";
        sprintf(buff, sql.c_str(), this->execStatusInfoTb.c_str(), item.second.id, item.second.status == 2 ? -1 : item.second.status, item.second.execZone, 
        item.second.systemNum.c_str(), item.second.arch.c_str(), item.second.note.c_str(), "now()",
        "now()", item.second.cpu.c_str(), item.second.ip.c_str(), item.second.port, item.second.web.empty()? "":item.second.web.c_str(), 
        item.second.os.c_str(), 100);
        MysqlExecRes_t ret;
        this->common->execSql(ret, string(buff));
        if(!ret.isSuccess) {
            logger->error("syncmysql error: {}", buff);
            return false;
        }
    }
    outer["all_exec_list"] = outerall;
    
    Json::Value outeractive;
    for (auto &item : this->suber->activeExecList) {
        Json::Value inneractive;
        inneractive["id"] = item.second.id;
        inneractive["status"] = item.second.status;
        inneractive["exec_zone"] = item.second.execZone;
        inneractive["system_num"] = item.second.systemNum;
        inneractive["arch"] = item.second.arch;
        inneractive["note"] = item.second.note;
        inneractive["cpu"] = item.second.cpu;
        inneractive["ip"] = item.second.ip;
        inneractive["port"] = item.second.port;
        inneractive["web"] = item.second.web.empty()?"":item.second.web;
        inneractive["os"] = item.second.os;
        inneractive["scweight"] = item.second.scweight;

        inneractive["active_time"] = item.second.activeTime;
        inneractive["failcount"] = item.second.failCount;
        outeractive.append(inneractive);

        this->syncExecLog(item.first, "",  "INITSCHEDULE", "ONLINE", "初始化调度上线");
    }
    outer["active_exec_list"] = outeractive;

    outer["judge_rule"] = this->suber->judgeRule;
    outer["judge_err_count"] = this->suber->judgeErrorCount;
    outer["schedule_rule"] = this->suber->scheduleRule;
    outer["schedule_interval"] = this->suber->scheduleInterval;
    outer["mimic_switch"] = this->suber->mimicSwitch;
    if(outer.empty()) {
        logger->error("json value is empty, pub error");
        return false;
    }

    string str = jsonwriter.write(outer);
    // str += "\r\n";
    char endbuf[2] = {'\r', '\n'};
    logger->info("pub to subscribe: jsonall len: {}, jsonactive len: {}, subs size: {}", outerall.size(), outeractive.size(), subscribeBevs.size());
    if(subscribeBevs.size() > 0) {
        for (auto itsub = subscribeBevs.begin(); itsub != subscribeBevs.end(); itsub++) {
            bufferevent_write(*itsub, str.c_str(), strlen(str.c_str()));
            bufferevent_write(*itsub, endbuf, 2);
            bufferevent_flush(*itsub, EV_WRITE, BEV_NORMAL);
        }
    } else {
        logger->debug("no suber connect");
    }
    return true;
}

void *MimicSchedule::cleanThread(void *arg) {
    pair<void *, string> *thatAndaddr = (pair<void *, string>*)arg;
    MimicSchedule *that = (MimicSchedule*)thatAndaddr->first;
    string addr = thatAndaddr->second;
    delete thatAndaddr;
    std::shared_ptr<spdlog::logger> logger = that->logger;

    uint64_t time = MimicUtil::getRandSeed();
    pthread_mutex_lock(&(that->mutexExecs));
    string ip = that->suber->allExecList[addr].ip;
    string execOs = that->portToExecOs[that->suber->allExecList[addr].port];
    pthread_mutex_unlock(&(that->mutexExecs));

    char buff[5096] = {0};
    string cmd;
    if(that->isHorizon) {
        cmd = "cd /opt/docker-compose/horizon-docker-compose/;";
        cmd += "docker-compose stop horizon-%s;";
        cmd += "docker-compose rm -f horizon-%s;";
        cmd += "docker-compose up -d horizon-%s;";
        cmd += "docker volume prune -f;";
        sprintf(buff, cmd.c_str(), execOs.c_str(), execOs.c_str(), execOs.c_str()); 
    } else if(that->isController) {
        cmd = "cd /opt/docker-compose/controller-docker-compose/;";
        cmd += "docker-compose stop rabbitmq-%s nova-%s;";
        cmd += "docker-compose rm -f rabbitmq-%s nova-%s;";
        cmd += "docker-compose up -d rabbitmq-%s nova-%s;";
        sprintf(buff, cmd.c_str(), execOs.c_str(), execOs.c_str(), execOs.c_str(), execOs.c_str(), execOs.c_str(), execOs.c_str());
    } else if(that->isKeystone) {
        cmd = "cd /opt/docker-compose/keystone-docker-compose/;";
        cmd += "docker-compose stop keystone-%s;";
        cmd += "docker-compose rm -f keystone-%s;";
        cmd += "docker-compose up -d keystone-%s;";
        sprintf(buff, cmd.c_str(), execOs.c_str(), execOs.c_str(), execOs.c_str());
    }

    string cmdNew = "ssh root@%s \"%s\" 2>&1";
    char buffNew[5096] = {0};
    sprintf(buffNew, cmdNew.c_str(), ip.c_str(), buff);

    logger->info("ssh and cmd: {}", buffNew);

    char ret[5096] = {0};
    MimicUtil::execCmd(ret, 5096, buffNew);

    logger->info("clean ret: {}", ret);
    pthread_mutex_lock(&(that->mutexExecs));
    that->suber->allExecList[addr].status = -1;
    that->updateExecStatusDb(that->suber->allExecList[addr].ip, that->suber->allExecList[addr].port, that->suber->allExecList[addr].status);
    that->preCleanTimes[addr] = MimicUtil::getRandSeed();
    pthread_mutex_unlock(&(that->mutexExecs));
}

void MimicSchedule::cleanExec(string addr) {
    logger->debug("Spawn clean thread");
    pair<void *, string> *thatAndaddr = new pair<void *, string>;
    thatAndaddr->first = this;
    thatAndaddr->second = addr;
    pthread_t ntid;
    pthread_create(&ntid, NULL, MimicSchedule::cleanThread, (void*)thatAndaddr);
    pthread_detach(ntid);
    logger->debug("clean thread end");
}

void MimicSchedule::execAutoReportCb(struct evhttp_request *req, void *arg)
{
    pair<void*, vector<execBaseInfo_t>::iterator> *thatAnditer = 
    (pair<void*, vector<execBaseInfo_t>::iterator>*)arg;
    MimicSchedule *that = (MimicSchedule*)thatAnditer->first;
    vector<execBaseInfo_t>::iterator iter = (vector<execBaseInfo_t>::iterator)thatAnditer->second;
    delete thatAnditer;
    string addr  = iter->ip + ":" + std::to_string(iter->port);
    std::shared_ptr<spdlog::logger> logger = that->logger;

    int status = -1;
    if (!req || !evhttp_request_get_response_code(req)) {
        status = -1;
    } else {
        if(that->isHorizon || that->isKeystone) {
            if(evhttp_request_get_response_code(req) == 200) status = 0;
            else status = -1;
        } else if(that->isController) {
            if(evhttp_request_get_response_code(req) == 401) status = 0;
            else status = -1;
        }
    }

    logger->debug("addr: {}, statusold: {}, statusnew: {}", that->suber->allExecList[addr].status, addr, status);

    pthread_mutex_lock(&(that->mutexExecs));
    if(that->isFirstStart) {
        that->suber->allExecList[addr].status = status;
        if(that->suber->allExecList.size() == that->execInfo.size()) {
            bool isStatusInitFinish = true;
            for(auto it = that->suber->allExecList.begin(); it != that->suber->allExecList.end(); it++) {
                if(it->second.status == -2) {
                    isStatusInitFinish = false;
                    break;
                }
            }
            if(isStatusInitFinish) {
               that->initActiveExec();
                if(that->initMysqlAndPub()) {
                    that->isFirstStart = false;
                    logger->info("First start init finish end");
                } else {
                    logger->error("initMysqlAndPub ERROR, reinit");
                    for(auto it = that->suber->allExecList.begin(); it != that->suber->allExecList.end(); it++) {
                        it->second.status = -2;
                    }
                }
                logger->info("active size: {}", that->suber->activeExecList.size());
            }
        }
    } else {
        if(that->suber->allExecList[addr].status == 1 && status == -1) {
            if(that->suber->activeExecList.find(addr) != that->suber->activeExecList.end()) {
                that->doUpdown(addr, "Schedule");
            } else logger->error("cat find this addr: {} in active exec list", addr);
        } else if(that->suber->allExecList[addr].status == 0 && status == -1) {
            that->suber->allExecList[addr].status = 2;
            that->cleanExec(addr);
        } else if(that->suber->allExecList[addr].status == -1 && status == 0) {
            that->suber->allExecList[addr].status = 0;
        } else if(that->suber->allExecList[addr].status == -1 && status == -1) {
            uint64_t currentTime = MimicUtil::getRandSeed();
            if((that->preCleanTimes.find(addr) == that->preCleanTimes.end()) || (currentTime - that->preCleanTimes[addr]) >= 60*1000000) {
                logger->debug("is find: {}", that->preCleanTimes.find(addr) == that->preCleanTimes.end());
                if(that->preCleanTimes.find(addr) != that->preCleanTimes.end()) {
                    logger->debug("ctime: {}, prtime: {}, jtime: {}", currentTime, that->preCleanTimes[addr], currentTime-that->preCleanTimes[addr]);
                }
                that->suber->allExecList[addr].status = 2;
                that->cleanExec(addr);
            }
        } else if( that->suber->allExecList[addr].status == -2) {
            logger->error("exec status -2 exit process loop");
            event_base_loopexit(that->evbase, NULL);
        }

        if(!(that->suber->allExecList[addr].status == 1 && status == -1))
            that->updateExecStatusDb(that->suber->allExecList[addr].ip, that->suber->allExecList[addr].port, that->suber->allExecList[addr].status);
        //上线补充
        if(that->suber->activeExecList.size() < that->globalConf->execNum) {
            int upexec_nums = (that->globalConf->execNum - that->manualDownNum)-that->suber->activeExecList.size();
            logger->debug("should up exec nums: {}", upexec_nums);
            for(int i = 0; i < upexec_nums; i++) {
                that->doUpdown("", "Schedule");
            }
        }
        logger->info("addr: {}, status: {}, active: {}, manualdownNUM, {}", addr, that->suber->allExecList[addr].status, that->suber->allExecList[addr].activeTime, 
        that->manualDownNum);
    }
    pthread_mutex_unlock(&(that->mutexExecs));
}

void MimicSchedule::execStatusReport()
{
    logger->info("Exec status req url: {}, timeout: {}", testUrl, this->baseInfo->reportInterval);
    int idCount = 0;
    for (auto iter = execInfo.begin(); iter != execInfo.end(); iter++) {
        pair<void*, vector<execBaseInfo_t>::iterator> *thatAnditer = new pair<void*, vector<execBaseInfo_t>::iterator>;
        thatAnditer->first = this;
        thatAnditer->second = iter;

        string addr  = iter->ip + ":" + std::to_string(iter->port);
        if(this->isFirstStart) {
            executor_t executorInfo;
            idCount += 1;

            executorInfo.id = idCount;
            executorInfo.status = -2;
            executorInfo.execZone = idCount;
            executorInfo.systemNum = "exec_"+ std::to_string(idCount);
            executorInfo.arch = iter->arch;
            executorInfo.note = "init";
            executorInfo.cpu = iter->arch;
            executorInfo.ip = iter->ip;
            executorInfo.port = iter->port;
            if(this->isHorizon) executorInfo.web = iter->web;
            executorInfo.os = iter->os;
            executorInfo.scweight = 100;

            executorInfo.activeTime = 0;
            executorInfo.failCount = 0;

            this->suber->allExecList.emplace(addr, std::move(executorInfo));
        }
        
        struct evhttp_connection *evcon = NULL;
        if(addrConn.find(addr) == addrConn.end()) {
            evcon = evhttp_connection_base_new(this->autoReportTimer->base, 
            NULL, (*iter).ip.c_str(), (*iter).port);
            if(evcon == NULL) {
                logger->error("Evcon error: {}:{}", (*iter).ip.c_str(), (*iter).port);
                delete thatAnditer;
                continue;
            }
            addrConn.emplace(addr, evcon);
        }
        evcon = addrConn[addr];

        evhttp_connection_set_timeout(evcon, this->baseInfo->reportInterval);
        struct evhttp_request *req = evhttp_request_new(MimicSchedule::execAutoReportCb, thatAnditer);
        if (req == NULL) {
            logger->error("executor error: {}:{}", (*iter).ip.c_str(), (*iter).port);
            delete thatAnditer;
            continue;
        }
        struct evkeyvalq *output_headers = evhttp_request_get_output_headers(req);
        evhttp_add_header(output_headers, "Host", (*iter).ip.c_str());
        evhttp_add_header(output_headers, "Connection", "close");
        logger->debug("make request, addr: {}:{}", (*iter).ip.c_str(), (*iter).port);
        int ret = evhttp_make_request(evcon, req, EVHTTP_REQ_GET, testUrl.c_str());
        if (ret != 0) {
            logger->error("make req executor error: {}:{}", (*iter).ip.c_str(), (*iter).port);
            delete thatAnditer;
        }
    }
}

void MimicSchedule::initActiveExec() {
    logger->debug("initActiveExec");
    this->suber->activeExecList.clear();
    for(int i = 0; i < this->globalConf->execNum - manualDownNum; i++){
        try {
            if (this->suber->scheduleRule == "random"){
                //全随机调度
                string key = policy_all_random();
                logger->info("{} >>>>>>>>>>> {}", "init-"+std::to_string(i), key);
            } else if(this->suber->scheduleRule == "scweight"){
                //权重调度
                string key = policy_weight();
                logger->info("{} >>>>>>>>>>> {}", "init-"+std::to_string(i), key);
                
            } else if(this->suber->scheduleRule == "host_random"){
                //主机+随机调度
                string key = policy_host_random();
                logger->info("{} >>>>>>>>>>> {}", "init-"+std::to_string(i), key);
            } else {
                logger->debug("cant find schedule policy");
                return;
            }
        } catch(const char *msg) {
            logger->debug("cant up exec: {}", msg);
        }
    }
}

//随机选择所有执行体中的一个，宿主机无关;1.传downAddr,上下线具体执行体 2.不传downAddr，从备用随机选择一个
string MimicSchedule::policy_all_random(string downAddr) {
    logger->debug("policy_all_random start, down addr{}", downAddr);
    //1.构造所有备用执行体列表
    vector<executor_t> execsInReadyStatus;
    for(auto it = this->suber->allExecList.begin(); it != this->suber->allExecList.end(); it++) {
        if (it->second.status == 0) {
            execsInReadyStatus.emplace_back(it->second);
        }
    }
    if (execsInReadyStatus.size() == 0) throw "no ready execs left";
    //2.从所有备用执行体列表中随机选择一个执行体
    uint64_t seed = MimicUtil::getRandSeed();
    std::srand(seed);
    int index = rand() % execsInReadyStatus.size();
    string key = execsInReadyStatus[index].ip + ":" + std::to_string(execsInReadyStatus[index].port);

    this->suber->allExecList[key].status = 1;
    this->suber->allExecList[key].activeTime = seed;
    this->suber->activeExecList.emplace(key, this->suber->allExecList[key]);
    
    if (downAddr != "") {
        this->suber->allExecList[downAddr].status = 2;
        this->suber->activeExecList.erase(downAddr);

    }
    return key;
}

//根据权重随机选择所有执行体中的一个，宿主机无关。
string MimicSchedule::policy_weight(string downAddr) {
    logger->debug("policy_weight start, downaddr: {}", downAddr);
    //1.构造所有备用执行体列表,和权重分布区间
    map<weightRange_t, executor_t> weightRangeToExec;
    int total_weight = 0;
    int startPoint = 0;
    for(auto it = this->suber->allExecList.begin(); it != this->suber->allExecList.end(); it++) {
        if (it->second.status == 0) {
            total_weight += it->second.scweight;
            weightRange_t weight;
            weight.start = startPoint;
            weight.end = startPoint + it->second.scweight;
            weightRangeToExec.emplace(weight, it->second);
            startPoint = weight.end;
        }
    }
    if (weightRangeToExec.size() == 0)  throw "no ready execs left";
    
    //从0到所有执行体权重值之和之间随机一个权重
    uint64_t seed = MimicUtil::getRandSeed();
    std::srand(seed);
    int rand_weight = rand() % total_weight;  
    //4.遍历权重范围的执行体map，根据随机权重所在权重区间选取对应的执行体   
    for(auto it = weightRangeToExec.begin(); it!=weightRangeToExec.end(); it++){
        if(it->first.start <= rand_weight && rand_weight < it->first.end){
            string key = it->second.ip + ":" + std::to_string(it->second.port);
            this->suber->allExecList[key].status = 1;
            this->suber->allExecList[key].activeTime = seed;
            this->suber->activeExecList.emplace(key, this->suber->allExecList[key]);
            if (downAddr != "") {  //指定下线执行体
                this->suber->allExecList[downAddr].status = 2;
                this->suber->activeExecList.erase(downAddr);
            }
            return key;
        }
    }
    return "";
}

//主机+随机策略，每次从不同的主机中选择一个执行体
string MimicSchedule::policy_host_random(string downAddr) {
    logger->debug("policy_host_random start {}", downAddr);
    set<string> usedHost; //已使用的主机
    for(auto iter = this->suber->activeExecList.begin(); iter != this->suber->activeExecList.end(); iter++) {
        if (iter->second.status == 1) {
            usedHost.insert(iter->second.ip);
        }
    }
    //1.构造以主机为key，执行体列表为value的map
    map<string, vector<executor_t>> ipGroup, allGroup;
    for(auto it = this->suber->allExecList.begin(); it != this->suber->allExecList.end(); it++) {
        //按主机分组
        if(allGroup.find(it->second.ip) == allGroup.end()) {
            vector<executor_t> vecinner;
            vecinner.emplace_back(it->second);
            allGroup.emplace(it->second.ip, std::move(vecinner));
        } else {
            allGroup[it->second.ip].emplace_back(it->second);
        }
        //备用执行体分组
        if(ipGroup.find(it->second.ip) == ipGroup.end()) {
            if(it->second.status == 0) {
                vector<executor_t> vecinner;
                vecinner.emplace_back(it->second);
                ipGroup.emplace(it->second.ip, std::move(vecinner));
            }
        } else {
            if(it->second.status == 0){
                ipGroup[it->second.ip].emplace_back(it->second);
            }
        }
    }
    //已经全部主机分布，上线执行体在下线主机上,此时downaddr不能为空
    if(usedHost.size() == this->globalConf->execNum) {
        if(downAddr == "") {
            throw "down addr is empty";
        }
        //优先当前主机上线
        string groupKey = this->suber->allExecList[downAddr].ip;
        if(ipGroup[groupKey].size() > 0) {
            uint64_t seed = MimicUtil::getRandSeed();
            std::srand(seed);
            int index = rand() % ipGroup[groupKey].size();
            string key = ipGroup[groupKey][index].ip + ":" + std::to_string(ipGroup[groupKey][index].port);
            this->suber->allExecList[key].status = 1;
            this->suber->allExecList[key].activeTime = seed;
            this->suber->activeExecList.emplace(key, this->suber->allExecList[key]);

            this->suber->allExecList[downAddr].status = 2;
            this->suber->activeExecList.erase(downAddr);
            return key;
        }
    }

    //生成最小上线数量列表
    vector<pair<string, int>> hostUpExecNumList;
    for(auto item : allGroup) {
        int upNum = 0;
        for(auto ele : item.second) {
            if(ele.status == 1) upNum += 1;
        }
        hostUpExecNumList.emplace_back(pair<string, int>(item.first, upNum));
    }
    //升序排序
    std::sort(hostUpExecNumList.begin(), hostUpExecNumList.end(), 
    [](pair<string, int>& pair1, pair<string, int>& pair2){return pair1.second < pair2.second;});
    //上线
    for(auto innerpair : hostUpExecNumList) {
        if(ipGroup[innerpair.first].size() > 0) {
            uint64_t seed = MimicUtil::getRandSeed();
            std::srand(seed);
            int index = rand() % ipGroup[innerpair.first].size();
            string key = ipGroup[innerpair.first][index].ip + ":" + std::to_string(ipGroup[innerpair.first][index].port);
            this->suber->allExecList[key].status = 1;
            this->suber->allExecList[key].activeTime = seed;
            this->suber->activeExecList.emplace(key, this->suber->allExecList[key]);

            if(downAddr != "") {
                this->suber->allExecList[downAddr].status = 2;
                this->suber->activeExecList.erase(downAddr);
            }
            return key;
        }
    }
    throw "cant find suitable exec";
}

void MimicSchedule::doUpdown(string addr, string msg) {
    if(this->suber->activeExecList.find(addr) != this->suber->activeExecList.end() && 
    this->suber->activeExecList.size() == 1) {
        logger->warn("only left one executor, dont do up down");
        return;
    }
    string action;
    if(msg != "Schedule") {
        msg = "JudgeError";
        action = "JUDGEERROR";
    }else{
        action = "AUTOSCHEDULE";
    }
    string downAddr = addr;
    logger->debug("downAddr: {}", downAddr);
    string key = "";

    if(msg == "JudgeError" && downAddr != "") {
        this->suber->allExecList[downAddr].failCount += 1;
        if(this->suber->allExecList[downAddr].failCount == INT_MAX) {
            this->suber->allExecList[downAddr].failCount = 1;
        }
    }

    try {
        if (this->suber->scheduleRule == "scweight"){
            key = policy_weight(downAddr);
        } else if( this->suber->scheduleRule == "random" ) {
            key = policy_all_random(downAddr);
        } else if(this->suber->scheduleRule == "host_random") {
            key = policy_host_random(downAddr);
        } else {
            logger->warn("no schedule policy specified");
            return;
        }
    } catch (const char* msg) {
        logger->warn("addr: {}, has no online exec to up, dont down the active one, msg: {}", downAddr, msg);
        if(downAddr != "") {
            this->suber->allExecList[downAddr].status = 2;
            this->suber->activeExecList.erase(downAddr);
        }
    }

    if(downAddr != "") {
        this->updateExecStatusDb(this->suber->allExecList[downAddr].ip, this->suber->allExecList[downAddr].port, this->suber->allExecList[downAddr].status);
        this->syncExecLog(downAddr, key, action, "OFFLINE", msg);
        this->cleanExec(downAddr);
    }
    if(key != "") {
        this->updateExecStatusDb(this->suber->allExecList[key].ip, this->suber->allExecList[key].port, this->suber->allExecList[key].status);
        this->syncExecLog(key, downAddr, action, "ONLINE", msg);
    }
    if(downAddr != "" || key != "") {
        this->syncAndPub(NULL);
        logger->info("{} >>>>>>>>>>> {}", downAddr, key);
    }
}

void MimicSchedule::execAtuoSchedule() {

    logger->debug("Exec auto schedule thread start");
    pthread_mutex_lock(&(this->mutexExecs));
    if(isFirstStart) {
        pthread_mutex_unlock(&(this->mutexExecs));
        return;
    }
    if(this->suber->activeExecList.empty()) {
        logger->warn("active exec list is empty!!!");
        pthread_mutex_unlock(&(this->mutexExecs));
        return;
    }
    uint64_t minTime = 0;
    for(auto iter = this->suber->activeExecList.begin(); iter != this->suber->activeExecList.end(); iter++) {
        if(minTime == 0) minTime = iter->second.activeTime;
        else if(iter->second.activeTime <= minTime) minTime = iter->second.activeTime;
    }
    for(auto iter = this->suber->activeExecList.begin(); iter != this->suber->activeExecList.end(); iter++) {
        if(iter->second.activeTime == minTime) {
            string downIp = iter->second.ip;
            string downAddr = iter->first;
            this->doUpdown(downAddr, "Schedule");
            break;
        }
    }
    pthread_mutex_unlock(&(this->mutexExecs));
}

void MimicSchedule::respMsg(struct bufferevent *bev, string status, Json::Value& value) {
    Json::Value valueRes;
    Json::FastWriter jsonwriter;
    valueRes["status"] = status;
    valueRes["msg"] = value.isNull() ? "" : value;
    string str = jsonwriter.write(valueRes);
    // str += "\r\n";
    char endbuf[2] = {'\r', '\n'};
    bufferevent_write(bev, str.c_str(), strlen(str.c_str()));
    bufferevent_write(bev, endbuf, 2);
    bufferevent_flush(bev, EV_WRITE, BEV_NORMAL);
}

void MimicSchedule::doManualUpOpt(struct bufferevent *bev, Json::Value& value) {
    logger->info("do manual up exec");
    if(!value.isObject()) {
        logger->error("need obj req data");
        this->respMsg(bev, "error", value);
        return;
    }
    string addr = value["addr"].asString();
    string opt = value["opt"].asString();
    if(opt != "up") {
        logger->error("not regeist this opt");
        this->respMsg(bev, "error", value);
        return;
    }
    pthread_mutex_lock(&(this->mutexExecs));
    if(this->suber->allExecList.find(addr) == this->suber->allExecList.end()) {
        logger->error("not regeist this addr");
        pthread_mutex_unlock(&(this->mutexExecs));
        this->respMsg(bev, "error", value);
        return;
    }
    if(this->suber->activeExecList.size() == this->globalConf->execNum) {
        logger->error("execs is largest");
        pthread_mutex_unlock(&(this->mutexExecs));
        this->respMsg(bev, "error", value);
        return;
    }

    if(this->suber->activeExecList.find(addr) != this->suber->activeExecList.end()) {
        logger->error("execs is already up");
        pthread_mutex_unlock(&(this->mutexExecs));
        this->respMsg(bev, "error", value);
        return;
    }
    this->suber->allExecList[addr].status = 1;
    this->suber->activeExecList.emplace(addr, this->suber->allExecList[addr]);

    this->manualDownNum = this->manualDownNum -1;

    this->updateExecStatusDb(this->suber->allExecList[addr].ip, this->suber->allExecList[addr].port, this->suber->allExecList[addr].status);
    this->syncExecLog(addr, "", "MANUALSCHEDULE", "ONLINE", "manually up one exec");
    this->syncAndPub(NULL);

    pthread_mutex_unlock(&(this->mutexExecs));
    logger->info("manual up exec >>>>>>>>>>>>>>> {}", addr);
    this->respMsg(bev, "ok", value);
}

void MimicSchedule::doManualDownOpt(struct bufferevent *bev, Json::Value& value) {
    logger->info("do manual down exec");
    if(!value.isObject()) {
        logger->error("need obj req data");
        this->respMsg(bev, "error", value);
        return;
    }
    string addr = value["addr"].asString();
    string opt = value["opt"].asString();
    if(opt != "down") {
        logger->error("not regeist this opt");
        this->respMsg(bev, "error", value);
        return;
    }
    pthread_mutex_lock(&(this->mutexExecs));
    if(this->suber->allExecList.find(addr) == this->suber->allExecList.end()) {
        logger->error("not regeist this addr");
        pthread_mutex_unlock(&(this->mutexExecs));
        this->respMsg(bev, "error", value);
        return;
    }
    if(this->suber->activeExecList.size() == 1) {
        logger->error("execs is least");
        pthread_mutex_unlock(&(this->mutexExecs));
        this->respMsg(bev, "error", value);
        return;
    }
    bool isactive = false;
    if(this->suber->activeExecList.find(addr) != this->suber->activeExecList.end()) isactive = true;
    if(!isactive) {
        logger->error("execs is not active ");
        pthread_mutex_unlock(&(this->mutexExecs));
        this->respMsg(bev, "error", value);
        return;
    }
    this->suber->allExecList[addr].status = 0;
    this->suber->activeExecList.erase(addr);

    this->manualDownNum += 1;

    this->updateExecStatusDb(this->suber->allExecList[addr].ip, this->suber->allExecList[addr].port, this->suber->allExecList[addr].status);
    this->syncExecLog(addr, "", "MANUALSCHEDULE", "OFFLINE", "manually down one exec");
    this->syncAndPub(NULL);

    pthread_mutex_unlock(&(this->mutexExecs));
    logger->info("manual down exec {} >>>>>>>>>>>>>>> null", addr);
    this->respMsg(bev, "ok", value);
}

void MimicSchedule::doManualCleanOpt(struct bufferevent *bev, Json::Value& value) {
    logger->info("do manual clean exec");
    if(!value.isObject()) {
        logger->error("need obj req data");
        this->respMsg(bev, "error", value);
        return;
    }
    string addr = value["addr"].asString();
    string opt = value["opt"].asString();
    if(opt != "clean") {
        logger->error("not regeist this opt");
        this->respMsg(bev, "error", value);
        return;
    }
    pthread_mutex_lock(&(this->mutexExecs));
    if(this->suber->allExecList.find(addr) == this->suber->allExecList.end()) {
        logger->error("not regeist this addr");
        pthread_mutex_unlock(&(this->mutexExecs));
        this->respMsg(bev, "error", value);
        return;
    }
    if(this->suber->allExecList[addr].status != -1) {
        logger->error("execs is online, not need to clean");
        pthread_mutex_unlock(&(this->mutexExecs));
        this->respMsg(bev, "error", value);
        return;
    }
    this->suber->allExecList[addr].status = 2;
    // pthread_mutex_unlock(&(this->mutexExecs));
    this->cleanExec(addr);

    // pthread_mutex_lock(&(this->mutexExecs));
    this->updateExecStatusDb(this->suber->allExecList[addr].ip, this->suber->allExecList[addr].port, this->suber->allExecList[addr].status);
    this->syncExecLog(addr, "", "MANUALCLEAN", "CLEAN", "manually clean one exec");
    pthread_mutex_unlock(&(this->mutexExecs));

    this->respMsg(bev, "ok", value);
}

void MimicSchedule::doReportError(struct bufferevent *bev, Json::Value& value) {
    logger->info("handle error exec");
    if(!value.isArray()) {
        logger->error("need array req data");
        this->respMsg(bev, "error", value);
        return;
    }
    for(int i = 0; i < value.size(); i++) {
        string addr = value[i]["addr"].asString();
        string msg = value[i]["msg"].asString();
        logger->info("error addr: {}, info: {}", addr, msg);
        pthread_mutex_lock(&(this->mutexExecs));
        if(this->suber->activeExecList.find(addr) == this->suber->activeExecList.end()) {
            logger->error("not in active list");
            pthread_mutex_unlock(&(this->mutexExecs));
            this->respMsg(bev, "error", value);
            return;
        }
        this->doUpdown(addr, msg);
        pthread_mutex_unlock(&(this->mutexExecs));
    }
    this->respMsg(bev, "ok", value);
}

void MimicSchedule::doQueryUpExecs(struct bufferevent *bev) {
    logger->info("do query upexecs");
    Json::Value valueRes;
    Json::FastWriter jsonwriter;
    for(auto iter = this->suber->activeExecList.begin(); iter != this->suber->activeExecList.end(); iter++) {
        valueRes.append(iter->first);
    }
    this->respMsg(bev, "ok", valueRes);
}

void MimicSchedule::doQueryExecStats(struct bufferevent *bev) {
    logger->info("do query execstatus");
    Json::Value valueRes;
    Json::FastWriter jsonwriter;
    for(auto iter = this->suber->allExecList.begin(); iter != this->suber->allExecList.end(); iter++) {
        string status;
        switch (iter->second.status)
        {
        case -1:
            status = "offline";
            break;
        case 0:
            status = "online";
            break;
        case 1:
            status = "active";
            break;
        case 2:
            status = "cleaning";
            break;
        default:
            status = "initing";
            break;
        }
        valueRes[iter->first] = status;
    }
    this->respMsg(bev, "ok", valueRes);
}

void MimicSchedule::doQueryUpTimes(struct bufferevent *bev) {
    logger->info("do query execs uptimes");
    Json::Value valueRes;
    Json::FastWriter jsonwriter;
    for(auto iter = this->suber->allExecList.begin(); iter != this->suber->allExecList.end(); iter++) {
        valueRes[iter->first] = iter->second.activeTime;
    }
    this->respMsg(bev, "ok", valueRes);
}

void MimicSchedule::doHungDown(struct bufferevent *bev, Json::Value& value) {
    logger->info("do manual hung-down");
    bool reportDown = true;
    bool scheduleDown = true;
    if(!this->autoReportTimer->isRuning) {
        logger->error("auto report is not runing");
        reportDown = false;
    }
    if(!this->autoScheduleTimer->isRuning) {
        logger->error("auto schdule is not runing");
        scheduleDown = false;
    }
    string service = value["service"].asString();
    if(service == this->baseInfo->serviceName) {
        if(reportDown) this->autoReportTimer->timerStop();
        if(scheduleDown) this->autoScheduleTimer->timerStop();
    } else {
        this->respMsg(bev, "error", value);
        return;
    }
    this->respMsg(bev, "ok", value);
}

void MimicSchedule::doHungUp(struct bufferevent *bev, Json::Value& value) {
    logger->info("do manual hung-up");
    bool reportUp = true;
    bool scheduleUp = true;
    if(this->autoReportTimer->isRuning) {
        logger->error("auto report is runing");
        reportUp = false;
    }
    if(this->autoScheduleTimer->isRuning) {
        logger->error("auto schedule is runing");
        scheduleUp = false;
    }
    string service = value["service"].asString();
    if(service == this->baseInfo->serviceName) {
        if(reportUp) this->autoReportTimer->timerSpawn();
        if(scheduleUp) this->autoScheduleTimer->timerSpawn();
    } else {
        this->respMsg(bev, "error", value);
        return;
    }
    this->respMsg(bev, "ok", value);
}

void MimicSchedule::doSubscribe(struct bufferevent *bev) {
    subscribeBevs.emplace_back(bev);
    syncAndPub(bev);
}

void MimicSchedule::registCallbacks()
{
    this->urlCallbackFun = NULL;
    this->threadInitCb = NULL;
    this->sigIntCb = MimicSchedule::sigIntCbFun;
}

int MimicSchedule::initTimer()
{
    autoReportTimer = new MimicTimer("auto_report_timer", 1, 
    2, dynamic_cast<MimicService*>(this));
    autoReportTimer->timerSpawn();
    autoScheduleTimer = new MimicTimer("auto_schedule_timer", 1, 
    atoi(suber->scheduleInterval.c_str()), dynamic_cast<MimicService*>(this));
    autoScheduleTimer->timerSpawn();
    return 0;
}

void MimicSchedule::writeCb(struct bufferevent *bev, void *arg) {
    MimicSchedule *that = (MimicSchedule*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    struct evbuffer *output = bufferevent_get_output(bev);
	if (evbuffer_get_length(output) == 0) {
        if(std::find(that->subscribeBevs.begin(), that->subscribeBevs.end(), bev) == that->subscribeBevs.end()) {
            logger->debug("Write ok");
        }
	} else {
		logger->debug("In writing");
    }
}

void MimicSchedule::doUpdateConfig(struct bufferevent *bev, Json::Value& value) {
    string preMimicSwitch = suber->mimicSwitch;
    suber->syncPolicyFromMysql();
    if(suber->mimicSwitch == "off" && preMimicSwitch != suber->mimicSwitch) {
        pthread_mutex_lock(&(this->mutexExecs));
        while(suber->activeExecList.size() > 1) {
            vector<string> activeListKeys;
            for(auto item : suber->activeExecList) {
                activeListKeys.emplace_back(item.first);
            }
            uint64_t seed = MimicUtil::getRandSeed();
            std::srand(seed);
            int index = rand() % activeListKeys.size();
            string downKey = activeListKeys[index];

            this->suber->allExecList[downKey].status = 0;
            suber->activeExecList.erase(downKey);
            this->manualDownNum += 1;
            logger->debug("mimic_switch down exec: {}", downKey);
        }
        this->syncAndPub(NULL);
        pthread_mutex_unlock(&(this->mutexExecs));
        this->respMsg(bev, "ok", value);
    } else if(suber->mimicSwitch == "on" && preMimicSwitch != suber->mimicSwitch) {
        pthread_mutex_lock(&(this->mutexExecs));
        this->manualDownNum = 0;
        this->syncAndPub(NULL);
        pthread_mutex_unlock(&(this->mutexExecs));
        this->respMsg(bev, "ok", value);
    } else {
        this->syncAndPub(NULL);
        this->respMsg(bev, "ok", value);
    }
}

void MimicSchedule::doUpdateScweight(struct bufferevent *bev, Json::Value& value) {
    suber->syncExecFromMysql();
    this->syncAndPub(NULL);
    this->respMsg(bev, "ok", value);
}

void MimicSchedule::readCb(struct bufferevent *bev, void *arg) {
    MimicSchedule *that = (MimicSchedule*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;

    if(that->suber->allExecList.empty() || 
    that->suber->activeExecList.empty()) {
        logger->warn("exec info sync fail");
    }

    while (true) {
        struct evbuffer *input = bufferevent_get_input(bev);
        size_t sz = evbuffer_get_length(input);
        logger->info("size receive: {}", sz);
        if(sz == 0) {
            bufferevent_flush(bev, EV_READ, BEV_NORMAL);
            break;
        }

        char *line = evbuffer_readln(input, NULL, EVBUFFER_EOL_CRLF);
        if(line == NULL || strlen(line) == 0) {
            // if(line != NULL || strlen(line) == 0) free(line);
            bufferevent_flush(bev, EV_READ, BEV_NORMAL);
            break;
        }
        string str = line;
        free(line);
        logger->debug("Receive line schedule: {}", str);
        Json::Value value;
        Json::Reader reader;
        try {
            reader.parse(str, value);
        } catch (...) {
            logger->error("invalid req data");
            continue;
        }
        string mode = value["mode"].asString();
        Json::Value data;
        Json::Value::Members members = value.getMemberNames();
        for(auto iter = members.begin(); iter != members.end(); iter++) {
            string key = *iter;
            if(key == "data") {
                data = value[key];
            }
        }
        if(mode == "manual_up" || mode == "manual_down" || 
        mode == "manual_clean" || mode == "report_error" || 
        mode == "manual_hung-up" || mode == "manual_hung-down") {
            if(data.empty()) {
                logger->error("data empty");
                bufferevent_free(bev);
            }
        }
        if(mode == "manual_up") {
            that->doManualUpOpt(bev, data);
        } else if(mode == "manual_down") {
            that->doManualDownOpt(bev, data);
        } else if(mode == "manual_clean") {
            that->doManualCleanOpt(bev, data);
        } else if(mode == "report_error") {
            that->doReportError(bev, data);
        } else if(mode == "query_upexecs") {
            that->doQueryUpExecs(bev);
        } else if(mode == "query_execstatus") {
            that->doQueryExecStats(bev);
        } else if(mode == "query_uptimes") {
            that->doQueryUpTimes(bev);
        } else if(mode == "manual_hung-up") {
            that->doHungUp(bev, data);
        } else if(mode == "manual_hung-down") {
            that->doHungDown(bev, data);
        } else if(mode == "update_config") {
            that->doUpdateConfig(bev, data);
        } else if(mode == "update_scweight") {
            that->doUpdateScweight(bev, data);
        } else if(mode == "subscribe") {
            that->doSubscribe(bev);
        } else {
            logger->error("not regeist this request");
            bufferevent_free(bev);
            break;
        }
    }
    logger->info("read cb end");
}

void MimicSchedule::eventCb(struct bufferevent *bev, short events, void *arg) {
    MimicSchedule *that = (MimicSchedule*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    if (events & BEV_EVENT_EOF) {
        logger->debug("connection close");
        if(std::find(that->subscribeBevs.begin(), that->subscribeBevs.end(), bev) != that->subscribeBevs.end()) {
            that->subscribeBevs.erase(std::find(that->subscribeBevs.begin(), that->subscribeBevs.end(), bev));
        } 
        bufferevent_free(bev);
    } else if(events & BEV_EVENT_ERROR) {
        logger->debug("connection error");
        if(std::find(that->subscribeBevs.begin(), that->subscribeBevs.end(), bev) != that->subscribeBevs.end()) {
            that->subscribeBevs.erase(std::find(that->subscribeBevs.begin(), that->subscribeBevs.end(), bev));
        }
        bufferevent_free(bev);
    } 
    //长连接不设置超时时间
    // else if(events & BEV_EVENT_TIMEOUT) {
    //     logger->error("Timout read or wirte");
    //     if(std::find(that->subscribeBevs.begin(), that->subscribeBevs.end(), bev) != that->subscribeBevs.end()) {
    //         that->subscribeBevs.erase(std::find(that->subscribeBevs.begin(), that->subscribeBevs.end(), bev));
    //     }
    //     bufferevent_free(bev);
    // }
}

void MimicSchedule::listenerCb(struct evconnlistener *listener, evutil_socket_t fd, 
struct sockaddr *addr, int len, void *ptr) {
    MimicSchedule *that = (MimicSchedule*)ptr;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    logger->debug("accpet cb fd");

    struct event_base* base = (struct event_base*)that->evbase;
    struct bufferevent *bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!bev) {
		logger->error("Error constructing bufferevent!");
		event_base_loopbreak(base);
		return;
	}

    bufferevent_setcb(bev, MimicSchedule::readCb, MimicSchedule::writeCb, MimicSchedule::eventCb, that);
    bufferevent_enable(bev, EV_READ);
    bufferevent_enable(bev, EV_WRITE);
    //长连接不设置超时时间
    // bufferevent_set_timeouts(bev, &(that->rTimeout), &(that->wTimeout));
}

void MimicSchedule::serviceSpawn()
{
    prctl(PR_SET_NAME, baseInfo->serviceName.c_str(), 
    NULL, NULL, NULL);
    evthread_use_pthreads();
    evbase = event_base_new();

    if(!initSubber()) exit(1);
    if(this->suber->mimicSwitch == "off") {
        this->manualDownNum = this->globalConf->execNum - 1;
    }
    initTimer();
    logger->debug("to bind listen socket");

    struct sockaddr_in serv;
 
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(baseInfo->port);
    serv.sin_addr.s_addr = inet_addr(baseInfo->host.c_str());
 

    evSigint = evsignal_new(evbase, SIGINT, sigIntCb, (void*)this);
    evsignal_add(evSigint, NULL);

    struct evconnlistener* listener;
    listener = evconnlistener_new_bind(evbase, MimicSchedule::listenerCb, this, 
                                  LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, 
                                  -1, (struct sockaddr*)&serv, sizeof(serv));
    logger->debug("to main loop");
    int ret = event_base_dispatch(evbase);
    logger->debug("loop exit: {}", ret);
    
    evconnlistener_free(listener);
    event_base_free(evbase);
}
