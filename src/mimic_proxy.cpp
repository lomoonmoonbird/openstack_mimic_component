#include <iostream>
#include <ctime>
#include <random>
#include <regex>
#include "json/json.h"
#include <sys/syscall.h>
#include <sys/types.h>

#include "mimic_proxy.hpp"
#include "mimic_struct.hpp"
#include "mimic_util.hpp"
#include "mimic_suber.hpp"


MimicProxy::MimicProxy(GlobalInfo_t *globalConf, 
vector<string> *ignoreKeys, ServiceBaseInfo_t *baseInfo)
{
    initProxyJudgeInfo(globalConf, ignoreKeys, baseInfo);

    isKeystone = false;
    string::size_type position;
    position = baseInfo->serviceName.find("keystone");
    if (position != string::npos) {
        isKeystone = true;
    }
    iscinder = false;
    position = baseInfo->serviceName.find("cinder");
    if (position != string::npos) {
        iscinder = true;
    }
    isneutron = false;
    position = baseInfo->serviceName.find("neutron");
    if (position != string::npos) {
        isneutron = true;
    }

    ishorizon = false;
    position = baseInfo->serviceName.find("horizon");
    if (position != string::npos) {
        ishorizon = true;
    }
}

MimicProxy::~MimicProxy() {
    if(commons.size() > 0) {
        for(auto iter = commons.begin(); iter!=commons.end();iter++){
            if((*iter) != nullptr){
                delete (*iter);
                *iter = nullptr;
            }
        }
    }
    if (subers.size() > 0) {
        for(auto iter = subers.begin(); iter!=subers.end();iter++){
            if((*iter) != nullptr){
                delete (*iter);
                *iter = nullptr;
            }
        }
    }
}

void MimicProxy::initTreadResource(evhtp_t *htp, evthr_t *thr, void *arg)
{
    MimicProxy *that = (MimicProxy*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    logger->debug("Thread resource init start: {}", 
    that->baseInfo->serviceName.c_str());

    MimicCommon *common = new MimicCommon(that->globalConf, logger);
    that->lockInPj();
    that->commons.emplace_back(common);
    that->unlockInPj();
    try{
        common->initMysql();
    } catch (const char* &e) {
        logger->error("mysql Init fail: {}", e);
        // that->lockInPj();
        // that->initThreadRes.emplace_back(false);
        // that->unlockInPj();
        // return;
    }
    // logger->debug("mysql Init success");

    MimicSuber* suber = new MimicSuber(common, dynamic_cast<MimicService*>(that), evthr_get_base(thr));
    that->lockInPj();
    that->initThreadRes.emplace_back(true);
    that->subers.emplace_back(suber);
    that->unlockInPj();
    usleep(2000000);
    suber->subscribe();
    
    /** test
    for (auto it = suber->activeExecList.begin(); it != suber->activeExecList.end();it++){
        logger->debug("bbbactive: {}, {}, {}", (*it).first, (*it).second.ip, (*it).second.port);
    }
    for (auto it = suber->allExecList.begin(); it != suber->allExecList.end();it++){
        logger->debug("cccactive: {}, {}, {}", (*it).first, (*it).second.ip, (*it).second.port);
    }
    */
    string inittid = std::to_string(syscall(SYS_gettid));
    suber->threadID = inittid;
    evthr_set_aux(thr, suber);
    logger->debug("Suber thread init success,thread id: {}", suber->threadID);
}

void MimicProxy::clearTask(string sid) {
    map<string, ProxyTask*>::iterator iter = proxyTaskList.find(sid);
    if(iter == proxyTaskList.end()) {
        logger->debug("this sid cant find task in proxy");
    } else {
        logger->debug("delete proxy task start: {}", sid);
        delete iter->second;
        lockInPj();
        proxyTaskList.erase(iter);
        unlockInPj();
        logger->debug("delete proxy task end: {}", sid);
    }
}

void MimicProxy::respToFrontNoProxyError(evhtp_request_t *req) {
    evhtp_headers_add_header(req->headers_out,evhtp_header_new("Content-Type", "application/json", 1, 1));
    const char * time_out_msg = "{\"request_msg\":\"Gateway Time Out\"}"; 
    evbuffer_add(req->buffer_out, time_out_msg, strlen(time_out_msg));
    evhtp_send_reply(req,EVHTP_RES_GWTIMEOUT);
}

void MimicProxy::respToFront(evhtp_request_t *req, ProxyTask* proxyTask) {
    LeftJudgeDada_t *judgeResult = proxyTask->judgeResult;

    logger->debug("resp to front start");
    string frontresp = "\n" + std::to_string(judgeResult->respStatus);
    //修正一些body长度为0但是content-length不为0所引起的超时问题
    if(judgeResult->bodyLen == 0) {
        if(judgeResult->respHeaders.find("Content-Length") != judgeResult->respHeaders.end()) {
            judgeResult->respHeaders["Content-Length"] = "0";
        }
        if(judgeResult->respHeaders.find("content-length") != judgeResult->respHeaders.end()) {
            judgeResult->respHeaders["content-length"] = "0";
        }
    }

    for(auto item : judgeResult->respHeaders) {
        frontresp += item.first + ":" + item.second +"\n";
        evhtp_headers_add_header(req->headers_out,evhtp_header_new(item.first.c_str(), item.second.c_str(), 1, 1));
    }
    logger->debug("sid: {}, headers to resp: {}", proxyTask->sid, frontresp);
    if(judgeResult->bodyLen > 0) {
        logger->debug("sid: {}, body to resp: {}", proxyTask->sid, judgeResult->body);
        evbuffer_add(req->buffer_out, judgeResult->body.c_str(), judgeResult->bodyLen);
    }
    logger->debug("resp to front end");
    evhtp_send_reply(req, judgeResult->respStatus);
    evhtp_request_resume(req);
}

void MimicProxy::makeSidTask(evhtp_request_t *req) {
    //sid生成
    logger->debug("in make task");
    lockInPj();
    uint64_t timeseed = MimicUtil::getRandSeed();
    std::default_random_engine e;
    std::uniform_int_distribution<int> u(1,1000000);
    e.seed(timeseed);
    string sid = std::to_string(timeseed) + std::to_string(u(e)) + "-" + baseInfo->serviceName;
    logger->debug("sid: {}", sid);
    //proxytask生成
    ProxyTask *proxyTask = NULL;
    if(proxyTaskList.find(sid) != proxyTaskList.end()) {
        logger->error("sid is already in use: {}", sid);
        respToFrontNoProxyError(req);
        unlockInPj();
        return;
    } else {
        proxyTask = new ProxyTask(sid, this, req);
        proxyTaskList.emplace(sid, proxyTask);
    }
    unlockInPj();
    //task start
    proxyTask->startTask();
}

string MimicProxy::convertFromCincerOrNeutron(string saddr) {
    regex pattern("(.*):(.*)");
    smatch results;
    string ip, port;
    if (regex_match(saddr, results, pattern)) {
        ip = results[1];
        port = results[2];
    } else {
        logger->error("parse ip port fail, dont send to schedule: {}", saddr);
        return saddr;
    }
    if(iscinder) {
        return ip + ":" + std::to_string(MimicUtil::cinderToNova(atoi(port.c_str())));
    } else if(isneutron) {
        return ip + ":" + std::to_string(MimicUtil::neutronToNova(atoi(port.c_str())));
    } else {
        return saddr;
    }
}

void respManualErr(evhtp_request_t *req) {
    const char * time_out_msg = "{\"request_msg\":\"invalid manual\"}"; 
    evhtp_headers_add_header(req->headers_out,evhtp_header_new("Content-Type", "application/json", 1, 1));
    evbuffer_add(req->buffer_out, time_out_msg, strlen(time_out_msg));
    evhtp_send_reply(req,EVHTP_RES_400);
}

void MimicProxy::deal_monitor_exec_manual(evhtp_request_t *req) {
    string backendVal1 = evhtp_header_find(req->headers_in, (const char *)"content-type");
    string backendVal2 = evhtp_header_find(req->headers_in, (const char *)"Content-Type");
    if(backendVal1.find("json") == std::string::npos && backendVal2.find("json") == std::string::npos) {
        respManualErr(req);
        return;
    }
    char *body = NULL;
    size_t bodyLen = 0;
    bodyLen = evbuffer_get_length(req->buffer_in);
    if(bodyLen > 0 && req->method != htp_method_HEAD) {
        body = (char *)malloc(bodyLen + 1);
        memset(body, 0x00,bodyLen + 1);
        evbuffer_copyout(req->buffer_in, body, bodyLen);
        if(strlen(body) != bodyLen) {
            logger->warn("body len not same");
        }
    }
    string bodystr = body;
    if(body != NULL) free(body);
    if(bodyLen = 0) {
        respManualErr(req);
        return;
    }

    Json::Reader reader;
    Json::Value value;
    try {
        reader.parse(bodystr, value);
    } catch (...) {
        logger->debug("json parse error");
        respManualErr(req);
        return;
    }

    if(value["opt_list"].size() != 1) {
        respManualErr(req);
        return;
    }

    string addrnew;
    string addr = value["opt_list"][0]["he_addr"].asString();
    string opt = value["opt_list"][0]["opt"].asString();
    regex pattern("(.*):(.*)");
    smatch results;
    string ip, port;
    if (regex_match(addr, results, pattern)) {
        ip = results[1];
        port = results[2];
    } else {
        logger->error("parse ip port fail: {}", addr);
        respManualErr(req);
        return;
    }

    if(port == "18776") {
        addrnew = ip + ":" + "18774";
    } else if(port == "28776") {
        addrnew = ip + ":" + "28774";
    } else if(port == "38776") {
        addrnew = ip + ":" + "38774";
    } else if(port == "19696") {
        addrnew = ip + ":" + "18774";
    } else if(port == "29696") {
        addrnew = ip + ":" + "28774";
    } else if(port == "39696") {
        addrnew = ip + ":" + "38774";
    } else {
        addrnew = ip + ":" + port;
    }

    Json::Value outer;
    Json::Value inner;
    Json::FastWriter writer;

    outer["mode"] = "manual_" + opt;
    inner["opt"] = opt;
    inner["addr"] = addrnew;
    outer["data"] = inner;
    string reqstr = writer.write(outer);

    string scip;
    int scprot;
    if(addrnew.find("8774") != std::string::npos) {
        scip = globalConf->schedule_controller_ip;
        scprot = globalConf->schedule_controller_port;
    }
    if(addrnew.find("5000") != std::string::npos) {
        scip = globalConf->schedule_keystone_ip;
        scprot = globalConf->schedule_keystone_port;
    }
    if(addrnew.find("388") != std::string::npos) {
        scip = globalConf->schedule_horizon_ip;
        scprot = globalConf->schedule_horizon_port;
    }

    logger->debug("reqstr: {}, scip: {}, scport: {}", reqstr, scip, scprot);
    MimicTcpClient *client = new MimicTcpClient(scip, scprot, reqstr, evthr_get_base(req->conn->thread), this, "");
    client->tcpRequest();
    logger->debug("send up down clean to schedule ok");
    const char *okresp = "{\"code\":200, \"msg\":\"\", \"success\":true, \"data\":{}}";
    evhtp_headers_add_header(req->headers_out,evhtp_header_new("Content-Type", "application/json", 1, 1));
    evbuffer_add(req->buffer_out, okresp, strlen(okresp));
    evhtp_send_reply(req,EVHTP_RES_200);
}

void MimicProxy::deal_monitor_config_policy(evhtp_request_t *req) {
    string backendVal1 = evhtp_header_find(req->headers_in, (const char *)"content-type");
    string backendVal2 = evhtp_header_find(req->headers_in, (const char *)"Content-Type");
    if(backendVal1.find("json") == std::string::npos && backendVal2.find("json") == std::string::npos) {
        respManualErr(req);
        return;
    }
    char *body = NULL;
    size_t bodyLen = 0;
    bodyLen = evbuffer_get_length(req->buffer_in);
    if(bodyLen > 0 && req->method != htp_method_HEAD) {
        body = (char *)malloc(bodyLen + 1);
        memset(body, 0x00,bodyLen + 1);
        evbuffer_copyout(req->buffer_in, body, bodyLen);
        if(strlen(body) != bodyLen) {
            logger->warn("body len not same");
        }
    }
    string bodystr = body;
    if(body != NULL) free(body);
    if(bodyLen = 0) {
        respManualErr(req);
        return;
    }

    Json::Reader reader;
    Json::Value value;
    try {
        reader.parse(bodystr, value);
    } catch (...) {
        logger->debug("json parse error");
        respManualErr(req);
        return;
    }

    MimicSuber *suber = (MimicSuber *)evthr_get_aux(req->conn->thread);
    string sql_query = "SELECT `conf_key`, `conf_value` FROM `configurable_mimic_config`;";
    MysqlExecRes_t res;
    suber->common->execSql(res, sql_query);
    if(!res.isSuccess) {
        respManualErr(req);
        return;
    }

    Json::Value::Members mem = value.getMemberNames(); 
    for (auto iter = mem.begin(); iter != mem.end(); iter++) {
        string key = *iter;
        for(auto itouter = res.result.begin(); itouter!=res.result.end(); itouter++) {
            //key
            auto itinnerKey = (*itouter).begin();
            //value
            auto itinnerVal = (*itouter).rbegin();
            //找到对应的key
            if ((*itinnerKey).second == key) {
                if (value[key].asString() != (*itinnerVal).second) {
                    string update_sql = "UPDATE `configurable_mimic_config` SET `conf_value`='%s' WHERE `conf_key`='%s';";
                    char buff[2048] = {0};
                    sprintf(buff, update_sql.c_str(), value[key].asString().c_str(), key.c_str());
                    MysqlExecRes_t ret;
                    suber->common->execSql(ret, string(buff));
                    if(!ret.isSuccess) {
                        logger->warn("update db maybe error: {}, may not insert not ok", buff);
                    } else {
                        logger->info("update db ok");
                    }

                    Json::FastWriter writer;
                    Json::Value outer;
                    Json::Value data;
                    outer["mode"] = "update_config";
                    data[key] = value[key];
                    outer["data"] = data;
                    string reqstr = writer.write(outer);

                    string ip;
                    int port;
                    if(key.find("_h") != std::string::npos) {
                        ip = this->globalConf->schedule_horizon_ip;
                        port = this->globalConf->schedule_horizon_port;
                    } else if(key.find("_n") != std::string::npos) {
                        ip = this->globalConf->schedule_controller_ip;
                        port = this->globalConf->schedule_controller_port;
                    } else {
                        ip = this->globalConf->schedule_keystone_ip;
                        port = this->globalConf->schedule_keystone_port;
                    }
                    MimicTcpClient *client = new MimicTcpClient(ip, port, reqstr, evthr_get_base(req->conn->thread), this, "");
                    client->tcpRequest();
                    break;
                }
            }
        }
    }
    logger->debug("send update config to schedule ok");
    const char *okresp = "{\"code\":200, \"msg\":\"\", \"success\":true, \"data\":{}}";
    evhtp_headers_add_header(req->headers_out,evhtp_header_new("Content-Type", "application/json", 1, 1));
    evbuffer_add(req->buffer_out, okresp, strlen(okresp));
    evhtp_send_reply(req,EVHTP_RES_200);
}

void MimicProxy::deal_monitor_config_scweight(evhtp_request_t *req) {
    string backendVal1 = evhtp_header_find(req->headers_in, (const char *)"content-type");
    string backendVal2 = evhtp_header_find(req->headers_in, (const char *)"Content-Type");
    if(backendVal1.find("json") == std::string::npos && backendVal2.find("json") == std::string::npos) {
        respManualErr(req);
        return;
    }
    char *body = NULL;
    size_t bodyLen = 0;
    bodyLen = evbuffer_get_length(req->buffer_in);
    if(bodyLen > 0 && req->method != htp_method_HEAD) {
        body = (char *)malloc(bodyLen + 1);
        memset(body, 0x00,bodyLen + 1);
        evbuffer_copyout(req->buffer_in, body, bodyLen);
        if(strlen(body) != bodyLen) {
            logger->warn("body len not same");
        }
    }
    string bodystr = body;
    if(body != NULL) free(body);
    if(bodyLen = 0) {
        respManualErr(req);
        return;
    }

    Json::Reader reader;
    Json::Value value;
    try {
        reader.parse(bodystr, value);
    } catch (...) {
        logger->debug("json parse error");
        respManualErr(req);
        return;
    }

    MimicSuber *suber = (MimicSuber *)evthr_get_aux(req->conn->thread);
    for(int i = 0; i != value["execs"].size(); i++) {
        string addr = value["execs"][i]["addr"].asString();
        regex pattern("(.*):(.*)");
        smatch results;
        string ip, port;
        if (regex_match(addr, results, pattern)) {
            ip = results[1];
            port = results[2];
        } else {
            logger->error("parse ip port fail: {}", addr);
            respManualErr(req);
            return;
        }
        int weight = value["execs"][i]["scweight"].asInt();
        string statusTb;
        string scip;
        int scport;
        if(port.find("388") != std::string::npos) {
            statusTb = "exec_status_info";
            scip = this->globalConf->schedule_horizon_ip;
            scport = this->globalConf->schedule_horizon_port;
        } else if(port.find("8774") != std::string::npos) {
            statusTb = "exec_status_info_n";
            scip = this->globalConf->schedule_controller_ip;
            scport = this->globalConf->schedule_controller_port;
        } else if(port.find("5000") != std::string::npos){
            statusTb = "exec_status_info_k";
            scip = this->globalConf->schedule_keystone_ip;
            scport = this->globalConf->schedule_keystone_port;
        } else {
            logger->debug("port err");
            respManualErr(req);
            return;
        }
        string sql_query = "SELECT `scweight` FROM `%s` WHERE `ip`=\'%s\' AND `port`=\'%s\';";
        char query_buff[5096] = {0};
        sprintf(query_buff, sql_query.c_str(), statusTb.c_str(), ip.c_str(), port.c_str());
        MysqlExecRes_t res;
        suber->common->execSql(res, query_buff);
        if(res.isSuccess) {
            // logger->debug("aa: {}, {}", res.result.begin()->begin()->second);
            if(atoi(res.result[0]["scweight"].c_str()) != weight) {
                string sql_update = "UPDATE `%s` SET `scweight`='%s' WHERE `ip`=\'%s\' AND `port`=\'%s\';";
                char update_buff[5096] = {0};
                logger->debug("aaa");
                sprintf(update_buff, sql_update.c_str(), statusTb.c_str(), std::to_string(weight).c_str(), ip.c_str(), port.c_str());
                MysqlExecRes_t res;
                suber->common->execSql(res, update_buff);
                if(!res.isSuccess) {
                    logger->error("maybe error affect 0 row");
                }
                Json::FastWriter writer;
                Json::Value outer;
                outer["mode"] = "update_scweight";
                outer["data"] = value;
                string reqstr = writer.write(outer);

                MimicTcpClient *client = new MimicTcpClient(scip, scport, reqstr, evthr_get_base(req->conn->thread), this, "");
                client->tcpRequest();
            }
        } else {
            logger->debug("port err");
            respManualErr(req);
            return;
        }
    }
    logger->debug("send update config to schedule ok");
    const char *okresp = "{\"code\":200, \"msg\":\"\", \"success\":true, \"data\":{}}";
    evhtp_headers_add_header(req->headers_out,evhtp_header_new("Content-Type", "application/json", 1, 1));
    evbuffer_add(req->buffer_out, okresp, strlen(okresp));
    evhtp_send_reply(req,EVHTP_RES_200);
}

void MimicProxy::deal_monitor_exec_get_horizon_policy(evhtp_request_t *req) {
    string sql_query = "SELECT `conf_key`, `conf_value` FROM `configurable_mimic_config` WHERE `conf_key` LIKE '%_h';" ;
    MimicSuber *suber = (MimicSuber *)evthr_get_aux(req->conn->thread);
    MysqlExecRes_t res;
    suber->common->execSql(res, sql_query);
    if(!res.isSuccess) {
        logger->debug("query err");
        respManualErr(req);
        return;
    }
    Json::FastWriter writer;
    Json::Value value;
    for(auto itouter = res.result.begin(); itouter!=res.result.end(); itouter++) {
        //key
        auto itinnerKey = (*itouter).begin();
        //value
        auto itinnerVal = (*itouter).rbegin();
        value[itinnerKey->second] = itinnerVal->second;
    }
    string jsonstr = writer.write(value);
    if ('\n'==jsonstr[strlen(jsonstr.c_str())-1]) 
        jsonstr[strlen(jsonstr.c_str())-1]=0;

    string respdata = "{\"code\":200, \"msg\":\"\", \"success\":true, \"data\":%s}";
    char buff[5096] = {0};
    sprintf(buff, respdata.c_str(), jsonstr.c_str());
    evhtp_headers_add_header(req->headers_out,evhtp_header_new("Content-Type", "application/json", 1, 1));
    evbuffer_add(req->buffer_out, buff, strlen(buff));
    evhtp_send_reply(req,EVHTP_RES_200);
}

void MimicProxy::deal_monitor_exec_get_nova_policy(evhtp_request_t *req) {
    string sql_query = "SELECT `conf_key`, `conf_value` FROM `configurable_mimic_config` WHERE `conf_key` LIKE '%_n';" ;
    MimicSuber *suber = (MimicSuber *)evthr_get_aux(req->conn->thread);
    MysqlExecRes_t res;
    suber->common->execSql(res, sql_query);
    if(!res.isSuccess) {
        logger->debug("query err");
        respManualErr(req);
        return;
    }
    Json::FastWriter writer;
    Json::Value value;
    for(auto itouter = res.result.begin(); itouter!=res.result.end(); itouter++) {
        //key
        auto itinnerKey = (*itouter).begin();
        //value
        auto itinnerVal = (*itouter).rbegin();
        value[itinnerKey->second] = itinnerVal->second;
    }
    string jsonstr = writer.write(value);
    if ('\n'==jsonstr[strlen(jsonstr.c_str())-1]) 
        jsonstr[strlen(jsonstr.c_str())-1]=0;

    string respdata = "{\"code\":200, \"msg\":\"\", \"success\":true, \"data\":%s}";
    char buff[5096] = {0};
    sprintf(buff, respdata.c_str(), jsonstr.c_str());
    evhtp_headers_add_header(req->headers_out,evhtp_header_new("Content-Type", "application/json", 1, 1));
    evbuffer_add(req->buffer_out, buff, strlen(buff));
    evhtp_send_reply(req,EVHTP_RES_200);
}

void MimicProxy::deal_monitor_exec_get_keystone_policy(evhtp_request_t *req) {
    string sql_query = "SELECT `conf_key`, `conf_value` FROM `configurable_mimic_config` WHERE `conf_key` LIKE '%_k';" ;
    MimicSuber *suber = (MimicSuber *)evthr_get_aux(req->conn->thread);
    MysqlExecRes_t res;
    suber->common->execSql(res, sql_query);
    if(!res.isSuccess) {
        logger->debug("query err");
        respManualErr(req);
        return;
    }
    Json::FastWriter writer;
    Json::Value value;
    for(auto itouter = res.result.begin(); itouter!=res.result.end(); itouter++) {
        //key
        auto itinnerKey = (*itouter).begin();
        //value
        auto itinnerVal = (*itouter).rbegin();
        value[itinnerKey->second] = itinnerVal->second;
    }
    string jsonstr = writer.write(value);
    if ('\n'==jsonstr[strlen(jsonstr.c_str())-1]) 
        jsonstr[strlen(jsonstr.c_str())-1]=0;

    string respdata = "{\"code\":200, \"msg\":\"\", \"success\":true, \"data\":%s}";
    char buff[5096] = {0};
    sprintf(buff, respdata.c_str(), jsonstr.c_str());
    evhtp_headers_add_header(req->headers_out,evhtp_header_new("Content-Type", "application/json", 1, 1));
    evbuffer_add(req->buffer_out, buff, strlen(buff));
    evhtp_send_reply(req,EVHTP_RES_200);
}

void MimicProxy::deal_config_mimic_switch(evhtp_request_t *req) {
    string backendVal1 = evhtp_header_find(req->headers_in, (const char *)"content-type");
    string backendVal2 = evhtp_header_find(req->headers_in, (const char *)"Content-Type");
    if(backendVal1.find("json") == std::string::npos && backendVal2.find("json") == std::string::npos) {
        respManualErr(req);
        return;
    }
    char *body = NULL;
    size_t bodyLen = 0;
    bodyLen = evbuffer_get_length(req->buffer_in);
    if(bodyLen > 0 && req->method != htp_method_HEAD) {
        body = (char *)malloc(bodyLen + 1);
        memset(body, 0x00,bodyLen + 1);
        evbuffer_copyout(req->buffer_in, body, bodyLen);
        if(strlen(body) != bodyLen) {
            logger->warn("body len not same");
        }
    }
    string bodystr = body;
    if(body != NULL) free(body);
    if(bodyLen = 0) {
        respManualErr(req);
        return;
    }

    Json::Reader reader;
    Json::Value value;
    try {
        reader.parse(bodystr, value);
    } catch (...) {
        logger->debug("json parse error");
        respManualErr(req);
        return;
    }

    string update_sql = "UPDATE `configurable_mimic_config` SET `conf_value`='%s' WHERE `conf_key`='mimic_switch';";
    char buff[2048] = {0};
    sprintf(buff, update_sql.c_str(), value["mimic_switch"].asString().c_str());
    MysqlExecRes_t ret;
    MimicSuber *suber = (MimicSuber *)evthr_get_aux(req->conn->thread);
    suber->common->execSql(ret, string(buff));
    if(!ret.isSuccess) {
        logger->warn("update db maybe error: {}, may not insert not ok", buff);
    } else {
        logger->info("update db ok");
    }

    Json::FastWriter writer;
    Json::Value outer;
    Json::Value data;
    outer["mode"] = "update_config";
    data["mimic_switch"] = value["mimic_switch"];
    outer["data"] = data;
    string reqstr = writer.write(outer);

    MimicTcpClient *clientScHrz = new MimicTcpClient(this->globalConf->schedule_horizon_ip, 
    this->globalConf->schedule_horizon_port, reqstr, evthr_get_base(req->conn->thread), this, "");
    clientScHrz->tcpRequest();

    MimicTcpClient *clientScCtr = new MimicTcpClient(this->globalConf->schedule_controller_ip, 
    this->globalConf->schedule_controller_port, reqstr, evthr_get_base(req->conn->thread), this, "");
    clientScCtr->tcpRequest();

    MimicTcpClient *clientScKst = new MimicTcpClient(this->globalConf->schedule_keystone_ip, 
    this->globalConf->schedule_keystone_port, reqstr, evthr_get_base(req->conn->thread), this, "");
    clientScKst->tcpRequest();

    const char *okresp = "{\"code\":200, \"msg\":\"\", \"success\":true, \"data\":{}}";
    evhtp_headers_add_header(req->headers_out,evhtp_header_new("Content-Type", "application/json", 1, 1));
    evbuffer_add(req->buffer_out, okresp, strlen(okresp));
    evhtp_send_reply(req,EVHTP_RES_200);
}

void MimicProxy::deal_get_mimic_switch(evhtp_request_t *req) {
    string sql_query = "SELECT `conf_key`, `conf_value` FROM `configurable_mimic_config` WHERE `conf_key` = 'mimic_switch';" ;
    MimicSuber *suber = (MimicSuber *)evthr_get_aux(req->conn->thread);
    MysqlExecRes_t res;
    suber->common->execSql(res, sql_query);
    if(!res.isSuccess) {
        logger->debug("query err");
        respManualErr(req);
        return;
    }
    Json::FastWriter writer;
    Json::Value value;
    for(auto itouter = res.result.begin(); itouter!=res.result.end(); itouter++) {
        //key
        auto itinnerKey = (*itouter).begin();
        //value
        auto itinnerVal = (*itouter).rbegin();
        value[itinnerKey->second] = itinnerVal->second;
    }
    string jsonstr = writer.write(value);
    if ('\n'==jsonstr[strlen(jsonstr.c_str())-1]) 
        jsonstr[strlen(jsonstr.c_str())-1]=0;

    string respdata = "{\"code\":200, \"msg\":\"\", \"success\":true, \"data\":%s}";
    char buff[5096] = {0};
    sprintf(buff, respdata.c_str(), jsonstr.c_str());
    evhtp_headers_add_header(req->headers_out,evhtp_header_new("Content-Type", "application/json", 1, 1));
    evbuffer_add(req->buffer_out, buff, strlen(buff));
    evhtp_send_reply(req,EVHTP_RES_200);
}

void MimicProxy::doManualOpt(evhtp_request_t *req, string manual) {
    logger->debug("manual: {}", manual);
    if (manual == "manual") {
        return deal_monitor_exec_manual(req);
    } else if(manual == "config_policy") {
        return deal_monitor_config_policy(req);
    } else if(manual == "config_scweight"){
        return deal_monitor_config_scweight(req);
    } else if(manual == "get_horizon_policy") {
        return deal_monitor_exec_get_horizon_policy(req);
    } else if(manual == "get_nova_policy") {
        return deal_monitor_exec_get_nova_policy(req);
    } else if(manual == "get_keystone_policy") {
        return deal_monitor_exec_get_keystone_policy(req);
    } else if(manual == "config_mimic_switch") {
        return deal_config_mimic_switch(req);
    } else if(manual == "get_mimic_switch") {
        return deal_get_mimic_switch(req);
    } else {
        evhtp_headers_add_header(req->headers_out,evhtp_header_new("Content-Type", "application/json", 1, 1));
        const char * time_out_msg = "{\"request_msg\":\"invalid manual\"}"; 
        evbuffer_add(req->buffer_out, time_out_msg, strlen(time_out_msg));
        evhtp_send_reply(req,EVHTP_RES_400);
    }
}

void MimicProxy::allReqCallback(evhtp_request_t *req, void *arg)
{
    MimicProxy *that = (MimicProxy*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    //检测服务是否正常
    logger->debug("Http msg come cb: {}: url:{}", that->baseInfo->serviceName.c_str(), 
    req->uri->path->full);
    MimicSuber *suber = (MimicSuber *)evthr_get_aux(req->conn->thread);
    if(suber->allExecList.empty() || suber->activeExecList.empty() || 
    suber->judgeErrorCount.empty() || suber->judgeRule.empty()) {
        logger->error("exec info and judge policy sync fail");
        evhtp_send_reply(req,EVHTP_RES_SERVERR);
        return;
    }
    if(that->ishorizon) {
        //手动上下线等
        const char *manualVal = evhtp_header_find(req->headers_in, (const char *)"manual");
        if(manualVal != NULL) {
            that->doManualOpt(req, manualVal);
            return;
        }
        //backend请求处理
        const char *backendVal = evhtp_header_find(req->headers_in, (const char *)"backend");
        if(backendVal != NULL) {
            //TODO doBackendReq()
            return;
        }
        //负载均衡请求特殊处理
        string uri = req->uri->path->full;
        if((uri == "/api/networks/lb_listeners" && evhtp_request_get_method(req) == htp_method_POST) || 
        (uri == "/api/networks/loadbalancer" && evhtp_request_get_method(req) == htp_method_POST)) {
            //TODO dolbListenerReq()
            // return;
        }
    }
    if (!that->ishorizon) {
        const char *horizonSid = evhtp_header_find(req->headers_in, (const char *)"sid");
        if(horizonSid != NULL) {
            logger->debug("horizon sid: {}", horizonSid);
        } else {
            logger->debug("no horizon sid");
        }
    }
    that->makeSidTask(req);
}

void MimicProxy::sigIntCbFun(int sig, short why, void *arg)
{
    MimicProxy *that = (MimicProxy*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;

    logger->debug("Stop service!");
    event_base_loopexit(that->evbase, NULL);
}

void MimicProxy::registCallbacks()
{
    this->urlCallbackFun = MimicProxy::allReqCallback;
    this->threadInitCb = MimicProxy::initTreadResource;
    this->sigIntCb = MimicProxy::sigIntCbFun;
}
