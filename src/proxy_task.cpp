#include <regex>
#include <iostream>
#include <cmath>
#include <sstream>
#include <time.h>
#include "mimic_task.hpp"
#include "mimic_proxy.hpp"
#include <event2/http.h>

ProxyTask::ProxyTask(string sid, MimicService* service, evhtp_request_t *frontreq):sid(sid), service(service) {
    this->mutexLeftJudge = PTHREAD_MUTEX_INITIALIZER;
    this->logger = service->logger;
    this->frontreq = frontreq;
    judgeResult = NULL;
    MimicProxy *proxy = dynamic_cast<MimicProxy*>(this->service);
    ignoreKeys = proxy->ignoreKeys;
    evbase = evthr_get_base(frontreq->conn->thread);
    memset(&rTimeout,0x00,sizeof(rTimeout));
    memset(&wTimeout,0x00,sizeof(wTimeout));
    this->rTimeout.tv_sec = proxy->baseInfo->timeout;
    this->wTimeout.tv_sec = proxy->baseInfo->timeout;
}

ProxyTask::~ProxyTask() {}

void ProxyTask::clearNewData() {
    logger->debug("clear data");
    for(auto item : judgeDataList) {
        // if(item.second->bodyLen > 0) {
        //     free(item.second->body.c_str());
        // }
        delete item.second;
    }
}

void ProxyTask::recordJudgeLog(string jsonstr) {
    logger->debug("record judge log: {}", jsonstr);
    MimicProxy *proxy = dynamic_cast<MimicProxy*>(this->service);

    Json::Reader reader;
    Json::Value value;
    try {
        reader.parse(jsonstr, value);
    } catch (...) {
        logger->error("json str pasre error, dont recort judge log");
        return;
    }

    string execids;
    string execerrids;
    string error_reason;
    string error_detail;
    Json::Value tmp;
    pthread_mutex_lock(&(suber->mutexInSuber));
    for(auto item : addrList) {
        // logger->debug("item in addrlist: {}, arch: {}, id: {}, sid: {}", item, suber->allExecList[item].arch, suber->allExecList[item].id, sid);
        execids += suber->allExecList[item].arch + "_" + std::to_string(suber->allExecList[item].id) + ",";
    }
    for(auto ele : value["data"]) {
        // logger->debug("ele in json addr: {}, arch: {}, id: {}, sid: {}", ele["addr"].asString(), suber->allExecList[ele["addr"].asString()].arch,suber->allExecList[ele["addr"].asString()].id, sid);
        string addr = ele["addr"].asString();
        if(proxy->iscinder) {
            regex pattern("(.*):(.*)");
            smatch results;
            string ip, port;
            if (regex_match(addr, results, pattern)) {
                ip = results[1];
                port = results[2];
                addr = ip + ":" + std::to_string(MimicUtil::novaToCinder(atoi(port.c_str())));
            }
        } else if(proxy->isneutron) {
            regex pattern("(.*):(.*)");
            smatch results;
            string ip, port;
            if (regex_match(addr, results, pattern)) {
                ip = results[1];
                port = results[2];
                addr = ip + ":" +  std::to_string(MimicUtil::novaToNeutron(atoi(port.c_str())));
            }
        }
        execerrids += suber->allExecList[addr].arch + "_" + std::to_string(suber->allExecList[addr].id) + ",";
        for (int i = 0; i < ele["error_reason"].size(); i++){
            error_reason += ele["error_reason"][i].asString() + ",";
        }
        for (int i = 0; i < ele["error_detail"].size(); i++){
            tmp.append(ele["error_detail"][i]);
        }    
    }
    error_reason.pop_back();
    error_detail = Json::FastWriter().write(tmp);
    if (!error_detail.empty() && error_detail.back() == '\n') {
        error_detail.pop_back();
    }
    pthread_mutex_unlock(&(suber->mutexInSuber));
    if(execids.empty() || execerrids.empty()) {
        logger->error("cant get exec ids");
        return;
    }
    execids[strlen(execids.c_str())-1]=0;
    execerrids[strlen(execerrids.c_str())-1]=0;

    string logTb;
    if(suber->isHorizon) {
        logTb = "judge_log_info";
    } else if(suber->isCinder) {
        logTb = "judge_log_info_n";
    } else if(suber->isNeutron) {
        logTb = "judge_log_info_n";
    } else if(suber->isNova) {
        logTb = "judge_log_info_n";
    } else if(suber->isKeystone) {
        logTb = "judge_log_info_k";
    } else {
        logger->error("cant find logtb");
        return;
    }
    if(logTb.empty()) {
        logger->error("cant find logtb");
        return;
    }

    // string error_reason;
    // string detailResult;
    // string judge_strategy;
    // if(judgeType == "majority") {
    //     error_reason = "Diff from other Executor by majority ruling";
    // } else {
    //     error_reason = "Executor response error";
    // }
    // detailResult = "Judge error touch of offline";
    // judge_strategy = "Majority ruling";

    stringstream sql;
    sql << "INSERT INTO `" << logTb.c_str() << "` SET ";
    sql << "`execs` = " << "\"" << execids.c_str() <<"\"" << ", ";
    sql << "`error_exec` = " << "\"" << execerrids.c_str() <<"\"" << ", ";
    sql << "`error_reason` = " << "\"" << error_reason.c_str() <<"\"" << ", ";
    sql << "`msg_type` = " << "\"" << urlfull.c_str() <<"\"" << ", ";
    sql << "`detail_result` = " << "'" << error_detail.c_str() << "'" << ", ";
    sql << "`judge_strategy` = " << "\"" << judgeType.c_str() <<"\"" << ", ";
    sql << "`create_time` = now()";
    MysqlExecRes_t ret;
    suber->common->execSql(ret, sql.str());
    if(!ret.isSuccess) {
        logger->warn("syncmysql maybe error: {}, may not insert not ok", sql.str());
        return;
    } else {
        logger->info("insert into judge log ok");
    }
    logger->info("record log end");
}

void ProxyTask::sendToSchedule() {
    logger->debug("send to schedule");
    if(analyseErrorDetailMap.empty()) {
        logger->info("no error exec, no need to send schedule and record log");
        return;
    }
    MimicProxy *proxy = dynamic_cast<MimicProxy*>(this->service);
    //record log
    Json::FastWriter writer;
    Json::Value outer;
    Json::Value data;
    Json::Reader reader;
    Json::Value errorDetailTmp;
    outer["mode"] = "report_error";
    for(auto item : analyseErrorDetailMap) {
        Json::Value inner;
        inner["addr"] = proxy->convertFromCincerOrNeutron(judgeDataList[item.first]->execAddr);
        inner["msg"] = item.second.error_reason;
        inner["error_reason"].append(item.second.error_reason);
        reader.parse( item.second.error_detail.c_str(), errorDetailTmp );
        inner["error_detail"].append(errorDetailTmp);
        data.append(inner);
    }
    outer["data"] = data;
    string reportStr = writer.write(outer);
    logger->debug("to record log json str: {}", reportStr);
    recordJudgeLog(reportStr);
    //send to schedule
    Json::FastWriter writer1;
    Json::Value outer1;
    Json::Value data1;
    outer1["mode"] = "report_error";
    string newreportstr;
    bool issend = false;
    logger->debug("sid: {}, 1111111111111111111111111111111111111111", sid);
    for(auto logprint : proxy->judgeErrNum) {
        logger->debug("first: {}, second: {}", logprint.first, logprint.second);
    }
    for(auto ele : analyseErrorDetailMap) {
        Json::Value inner1;
        logger->debug("ele.first: {}, execaddr: {}", ele.first, judgeDataList[ele.first]->execAddr);
        if(proxy->judgeErrNum.find(judgeDataList[ele.first]->execAddr) == proxy->judgeErrNum.end()) {
            logger->debug("aaaaaaaaaaaaa");
            proxy->judgeErrNum.emplace(judgeDataList[ele.first]->execAddr, 1);
            logger->debug("bbbbbbbbbbbbbbbb");
        } else {
            logger->debug("jjjjjjjjjjjjjjjjjjjjjjjjjj");
            if(proxy->judgeErrNum[judgeDataList[ele.first]->execAddr] < atoi(suber->judgeErrorCount.c_str())) {
                logger->debug("cccccccccccccccccc");
                proxy->judgeErrNum[judgeDataList[ele.first]->execAddr] += 1;
                logger->debug("ddddddddddddddddddddddd");
            } else {
                logger->debug("eeeeeeeeeeeeeeeeeeeeee");
                proxy->judgeErrNum[judgeDataList[ele.first]->execAddr] = 0;
                logger->debug("fffffffffffffffffffffffff");
                inner1["addr"] = proxy->convertFromCincerOrNeutron(judgeDataList[ele.first]->execAddr);
                logger->debug("gggggggggggggggggggg");
                inner1["msg"] = ele.second.error_reason;
                logger->debug("hhhhhhhhhhhhhhhhhhh");
                data1.append(inner1);
                logger->debug("iiiiiiiiiiiiiiiiiiiiiiii");
                issend = true;
            }
        }
    }
    logger->debug("sid: {}, 22222222222222222222222222222222222222222222", sid);
    outer1["data"] = data1;
    newreportstr = writer1.write(outer1);
    if(issend) {
        string ip;
        int port;
        if(suber->isController) {
            ip = proxy->globalConf->schedule_controller_ip;
            port = proxy->globalConf->schedule_controller_port;
        } else if(suber->isHorizon) {
            ip = proxy->globalConf->schedule_horizon_ip;
            port = proxy->globalConf->schedule_horizon_port;
        } else if(suber->isKeystone) {
            ip = proxy->globalConf->schedule_keystone_ip;
            port = proxy->globalConf->schedule_keystone_port;
        } else {
            logger->error("can't find schedule ip and port ,dont send to schedule");
        }
        logger->debug("sid: {}, 3333333333333333333333333333333333333333333333", sid);
        MimicTcpClient *client = new MimicTcpClient(ip, port, newreportstr, evbase, proxy, sid);
        client->tcpRequest();
    }
}

int ProxyTask::fillUpHeadesMap(evhtp_kv_t * kvobj, void * arg) {
    map<string, string> *headersMap = (map<string, string> *)arg;
    // (*headersMap)[kvobj->key] = kvobj->val;
    headersMap->emplace(kvobj->key, kvobj->val);
    return 0;
}

void ProxyTask::addErrorJudgeData(int countp, string execAddr) {
    MimicProxy *proxy = dynamic_cast<MimicProxy*>(this->service);
    LeftJudgeDada_t *leftJudgeData = new LeftJudgeDada_t;

    leftJudgeData->respStatus = EVHTP_RES_GWTIMEOUT;
    leftJudgeData->respHeaders["Content-Type"] = "application/json";
    leftJudgeData->contentType = "application/json";
    leftJudgeData->body = "{\"request_msg\":\"Gateway Time Out\"}";
    leftJudgeData->bodyLen = strlen(leftJudgeData->body.c_str());
    leftJudgeData->group = -1;
    leftJudgeData->key = sid + "-" + execAddr + "-" + std::to_string(countp);
    leftJudgeData->sid = sid;
    leftJudgeData->execAddr = execAddr;
    leftJudgeData->urlfull = this->urlfull;
    leftJudgeData->params = this->params;
    leftJudgeData->method = this->httpmethod;

    pthread_mutex_lock(&(this->mutexLeftJudge));
    judgeDataList.emplace(sid + "-" + execAddr + "-" + std::to_string(countp), leftJudgeData);
    int ret = judgeData(sid + "-" + execAddr + "-" + std::to_string(countp));
    pthread_mutex_unlock(&(this->mutexLeftJudge));
    if (ret == 1) proxy->respToFront(frontreq, this);

    //分析裁决结果并记录日志与发送调度
    if(this->judgeDataList.size() == this->addrList.size()) {
        this->analyseJudgeResult();
        this->clearNewData();
    }
}

int ProxyTask::initHeaders(vector<string>& addrList, map<string, string>& reqHeaders) {
    MimicProxy *proxy = dynamic_cast<MimicProxy*>(this->service);
    //生成server-array
    Json::Value serverArray;
    Json::FastWriter jsonwriter;
    suber = (MimicSuber *)evthr_get_aux(frontreq->conn->thread);
    pthread_mutex_lock(&(suber->mutexInSuber));
    for(auto iter = suber->activeExecList.begin(); iter != suber->activeExecList.end(); iter ++) {
        Json::Value serverArrayInner;
        serverArrayInner["ip"] = iter->second.ip;
        serverArrayInner["port"] = std::to_string(iter->second.port);
        serverArray.append(serverArrayInner);
        addrList.emplace_back(iter->first);
        failcountList.emplace(iter->first, iter->second.failCount);
        uptimelist.emplace(iter->first, iter->second.activeTime);
    }
    thrId = suber->threadID;
    judgeRule = suber->judgeRule;
    pthread_mutex_unlock(&(suber->mutexInSuber));
    logger->debug("judge rule: {}", judgeRule);
    string serverarraystr = jsonwriter.write(serverArray);
    if ('\n'==serverarraystr[strlen(serverarraystr.c_str())-1]) 
        serverarraystr[strlen(serverarraystr.c_str())-1]=0;
    //赋值
    reqHeaders["sid"] = sid;
    reqHeaders["Server-Array"] = serverarraystr;
    reqHeaders["Cache-Control"] = "no-cache";
    reqHeaders["pragma"] = "no-cache";
    //keystone特殊处理
    if(proxy->isKeystone) {
        char expires_at_timestrap[512] = {0};
        char audit_id[512] = {0};
        char IV[512] = {0};

        char value[5096] = {0};
        MimicUtil::execCmd(value, 5096, (char *)"python /opt/keystone_util.py 2>/dev/null");
        logger->debug("keystone_util exec ret: {}", value);

        char *p1 = expires_at_timestrap;
        char *p2 = audit_id;
        char *p3 = IV;
        int next = 1;
        for (int i = 0; i < strlen(value);) {
            if(value[i] == ':') {
                if(value[i+1] == ':' && value[i+2] == ':') {
                    i += 3;
                    next += 1;
                    continue;
                } else {
                    if(next == 1) {*p1 = value[i]; p1 += 1;}
                    if(next == 2) {*p2 = value[i]; p2 += 1;}
                    if(next == 3) {*p3 = value[i]; p3 += 1;}
                }
            } else {
                if(next == 1) {*p1 = value[i]; p1 += 1;}
                if(next == 2) {*p2 = value[i]; p2 += 1;}
                if(next == 3) {*p3 = value[i]; p3 += 1;}
            }
            i += 1;
        }
        IV[strlen(IV)-1] = 0;
        if(strlen(expires_at_timestrap) == 0 || strlen(audit_id) == 0 || strlen(IV) == 0) {
            logger->error("keystone_util exec fail: {}", sid);
            proxy->respToFrontNoProxyError(frontreq);
            return 1;
        }

        reqHeaders["Content-Type"] = "application/json";
        reqHeaders["p-time-stamp"] = expires_at_timestrap;
        reqHeaders["fingerprint"] = audit_id;
        reqHeaders["fingerprint2"] = IV;
    }

    struct sockaddr_in* client = (sockaddr_in*)frontreq->conn->saddr;
    string clientIp = inet_ntoa(client->sin_addr);
    uint16_t clientPort = client->sin_port;
    Json::Value reqInfo;
    Json::FastWriter jsonwriterreqinfo;
    reqInfo["source_ip"] = clientIp;
    reqInfo["source_port"] = clientPort;
    reqInfo["scheme"] = "http";
    string reqinfostr = jsonwriterreqinfo.write(reqInfo);
    if ('\n'==reqinfostr[strlen(reqinfostr.c_str())-1]) reqinfostr[strlen(reqinfostr.c_str())-1]=0;
    reqHeaders["Req-Info"] = reqinfostr;
    logger->debug("reqinfo: {}", reqinfostr);
    return 0;
}

void ProxyTask::startTask() {
    MimicProxy *proxy = dynamic_cast<MimicProxy*>(this->service);
    //请求url生成
    urlfull = frontreq->uri->path->full;
    char send_url[10240];
    memset(send_url,0x00,sizeof(send_url));
    if(frontreq->uri->query_raw==NULL) {
        sprintf(send_url,"%s",frontreq->uri->path->full);
    } else {
        sprintf(send_url,"%s?%s",frontreq->uri->path->full,frontreq->uri->query_raw);
    }
    //初始化请求头
    // vector<string> addrList;
    // map<string, string> reqHeaders;
    evhtp_kvs_for_each(frontreq->headers_in, ProxyTask::fillUpHeadesMap, &reqHeaders);
    if (initHeaders(addrList, reqHeaders)) return;
    //打印日志
    string headersraw = "\n" + MimicUtil::getMethonString(evhtp_request_get_method(frontreq)) + " " + urlfull;
    httpmethod = evhtp_request_get_method(frontreq);
    for(auto ele : reqHeaders) {
        headersraw += ele.first + ":" + ele.second + "\n";
    }
    logger->debug("sid: {}, reqheadersraw: {}", sid, headersraw);
    //请求体生成
    int frontend_body_len = evbuffer_get_length(frontreq->buffer_in);
    char *frontend_body = NULL;
    if(frontend_body_len > 0) {
        frontend_body = (char *)malloc(frontend_body_len + 1);
        memset(frontend_body, 0x00, frontend_body_len + 1);
        evbuffer_copyout(frontreq->buffer_in, frontend_body, frontend_body_len);
        params = frontend_body;
        logger->debug("sid: {}, req front body: {}", sid, frontend_body);
    }
    evhtp_request_pause(frontreq);
    //开始分发
    int countp = 0;
    for(auto it = addrList.begin(); it != addrList.end(); it++) {
        //解析ip port
        countp += 1;
        regex pattern("(.*):(.*)");
        smatch results;
        string ip, port;
        if (regex_match(*it, results, pattern)) {
            ip = results[1];
            port = results[2];
        } else {
            logger->error("parse ip port fail: {}", *it);
            addErrorJudgeData(countp, *it);
            continue;
        }
        logger->debug("{}, proxy to {}:{}", sid, ip, port);

        //创建连接对象
        evhtp_connection_t * conn=evhtp_connection_new(evbase, ip.c_str(), atoi(port.c_str()));
        if(conn==NULL) {
            logger->error("evhtp conn fail");
            addErrorJudgeData(countp, *it);
            continue;
        }
        // evhtp_connection_set_hook(conn, evhtp_hook_on_connection_fini, (evhtp_hook)ProxyTask::proxyConnFiniCb, this);
        evhtp_connection_set_timeouts(conn,&(rTimeout),&(wTimeout));

        //创建临时参数对象
        LeftTmpArgs_t *args = new LeftTmpArgs_t;
        args->coutp = countp;
        args->execAddr = *it;
        args->proxyTask = this;
        
        //创建请求对象
        evhtp_request_t *request = evhtp_request_new(ProxyTask::proxyReqCb, args);
        if(!request) {
            delete args;
            addErrorJudgeData(countp, *it);
            continue;
        }
        request->method = evhtp_request_get_method(frontreq);
        //设置回调
        evhtp_request_set_hook(request, evhtp_hook_on_error,(evhtp_hook)ProxyTask::proxyReqErrCb, args);
        evhtp_request_set_hook(request, evhtp_hook_on_request_fini,(evhtp_hook)ProxyTask::proxyReqFinishCb, args);

        //增加请求头
        for(auto item : reqHeaders) {
            if(item.first == "If-Modified-Since" || 
            item.first == "Range" || item.first == "If-Range" || 
            item.first == "If-None-Match") continue;
            evhtp_headers_add_header(request->headers_out,evhtp_header_new(item.first.c_str(), item.second.c_str(), 1, 1));
        }
        evhtp_headers_add_header(request->headers_out,evhtp_header_new("Mimic-Host", (*it).c_str(), 1, 1));

        //增加请求体
        if(frontend_body_len > 0) evbuffer_add(request->buffer_out, frontend_body, frontend_body_len); 

        //发起请求
        if(evhtp_make_request(conn,request, request->method, send_url) == 0) {
            logger->debug("send to executor : {} success", *it);
        } else {
            logger->debug("send to executor : {} fail", *it);
            delete args;
            addErrorJudgeData(countp, *it);
        }
    }
    //释放请求体内存
    if (frontend_body) free(frontend_body);
}

void ProxyTask::proxyReqCb(evhtp_request_t * req, void * arg) {
    LeftTmpArgs_t *args = (LeftTmpArgs_t *)arg;
    ProxyTask *that = (ProxyTask *)args->proxyTask;
    string execAddr = args->execAddr;
    int countp = args->coutp;
    //相关变量获取
    MimicProxy *proxy = dynamic_cast<MimicProxy*>(that->service);
    std::shared_ptr<spdlog::logger> logger = that->logger;
    //返回状态码
    unsigned int status = evhtp_request_status(req);
    //返回resp body
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
    //构造裁决信息结构体
    LeftJudgeDada_t *leftJudgeData = new LeftJudgeDada_t;
    leftJudgeData->respStatus = status;
    //打印日志
    evhtp_kvs_for_each(req->headers_in, ProxyTask::fillUpHeadesMap, &(leftJudgeData->respHeaders));
    string respHeadersRaw = std::to_string(status) + "\n";
    for(auto header : leftJudgeData->respHeaders) {
        respHeadersRaw += header.first + ":" + header.second + "\n";
    }
    logger->debug("sid: {}, header resp : {}", that->sid, respHeadersRaw);
    logger->debug("sid : {}, body len: {}, body: {}", that->sid, bodyLen, body == NULL ? "" : body);

    if(leftJudgeData->respHeaders.find("Content-Type") != leftJudgeData->respHeaders.end()) {
        leftJudgeData->contentType = leftJudgeData->respHeaders["Content-Type"];
    } else if(leftJudgeData->respHeaders.find("content-type") != leftJudgeData->respHeaders.end()) {
        leftJudgeData->contentType = leftJudgeData->respHeaders["content-type"];
    } else {
        leftJudgeData->contentType = "";
    }
    leftJudgeData->body = body == NULL ? "" : body;
    if(body != NULL) free(body);
    leftJudgeData->bodyLen = bodyLen;
    leftJudgeData->group = -1;
    leftJudgeData->key = that->sid + "-" + execAddr + "-" + std::to_string(countp);
    leftJudgeData->sid = that->sid;
    leftJudgeData->execAddr = execAddr;
    leftJudgeData->urlfull = that->urlfull;
    leftJudgeData->params = that->params;
    leftJudgeData->method = that->httpmethod;

    //裁决信息放入容器
    pthread_mutex_lock(&(that->mutexLeftJudge));
    that->judgeDataList.emplace(that->sid + "-" + execAddr + "-" + std::to_string(countp), leftJudgeData);
    //裁决
    int ret = that->judgeData(that->sid + "-" + execAddr + "-" + std::to_string(countp));
    pthread_mutex_unlock(&(that->mutexLeftJudge));
    //有裁决结果进行回复前端
    if (ret == 1) proxy->respToFront(that->frontreq, that);

    //分析裁决结果并记录日志与发送调度
    if(that->judgeDataList.size() == that->addrList.size()) {
        that->analyseJudgeResult();
        that->clearNewData();
    }
    //释放资源
    evhtp_request_free(req);
}

evhtp_res ProxyTask::proxyReqErrCb(evhtp_request_t * req, evhtp_error_flags errtype, void * arg){
    LeftTmpArgs_t *args = (LeftTmpArgs_t *)arg;
    ProxyTask *that = (ProxyTask *)args->proxyTask;
    string execAddr = args->execAddr;
    int countp = args->coutp;
    //相关变量获取
    MimicProxy *proxy = dynamic_cast<MimicProxy*>(that->service);
    std::shared_ptr<spdlog::logger> logger = that->logger;

    unsigned int status = evhtp_request_status(req);
    if(status != 0) {
        //返回resp body
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
        //构造裁决信息结构体
        LeftJudgeDada_t *leftJudgeData = new LeftJudgeDada_t;
        leftJudgeData->respStatus = status;
        evhtp_kvs_for_each(req->headers_in, ProxyTask::fillUpHeadesMap, &(leftJudgeData->respHeaders));
        string respHeadersRaw = std::to_string(status) + "\n";
        for(auto header : leftJudgeData->respHeaders) {
            respHeadersRaw += header.first + ":" + header.second + "\n";
        }
        logger->debug("sid: {}, header resp : {}", that->sid, respHeadersRaw);
        logger->debug("sid: {}, body len: {}, body: {}", that->sid, bodyLen, body == NULL ? "" : body);
        if(leftJudgeData->respHeaders.find("Content-Type") != leftJudgeData->respHeaders.end()) {
            leftJudgeData->contentType = leftJudgeData->respHeaders["Content-Type"];
        } else if(leftJudgeData->respHeaders.find("content-type") != leftJudgeData->respHeaders.end()) {
            leftJudgeData->contentType = leftJudgeData->respHeaders["content-type"];
        } else {
            leftJudgeData->contentType = "";
        }
        leftJudgeData->body = body == NULL ? "" : body;
        if(body != NULL) free(body);
        leftJudgeData->bodyLen = bodyLen;
        leftJudgeData->group = -1;
        leftJudgeData->key = that->sid + "-" + execAddr + "-" + std::to_string(countp);
        leftJudgeData->sid = that->sid;
        leftJudgeData->execAddr = execAddr;
        leftJudgeData->urlfull = that->urlfull;
        leftJudgeData->params = that->params;
        leftJudgeData->method = that->httpmethod;
        //裁决信息放入容器
        pthread_mutex_lock(&(that->mutexLeftJudge));
        that->judgeDataList.emplace(that->sid + "-" + execAddr + "-" + std::to_string(countp), leftJudgeData);
        //裁决
        int ret = that->judgeData(that->sid + "-" + execAddr + "-" + std::to_string(countp));
        pthread_mutex_unlock(&(that->mutexLeftJudge));
        //有裁决结果进行回复前端
        if (ret == 1) proxy->respToFront(that->frontreq, that);

        //分析裁决结果并记录日志与发送调度
        if(that->judgeDataList.size() == that->addrList.size()) {
            that->analyseJudgeResult();
            that->clearNewData();
        }
    } else {
        logger->error("sid: {} in error cb errtype: {}", that->sid, errtype);
        if(that->judgeDataList.find(that->sid + "-" + execAddr + "-" + std::to_string(countp)) == that->judgeDataList.end()) {
            that->addErrorJudgeData(countp, execAddr);
        }
    }
    //释放资源
    evhtp_request_free(req);
}

evhtp_res ProxyTask::proxyReqFinishCb(evhtp_request_t * req, void * arg){
    LeftTmpArgs_t *args = (LeftTmpArgs_t *)arg;
    ProxyTask *that = (ProxyTask *)args->proxyTask;
    string execAddr = args->execAddr;
    int countp = args->coutp;
    //清理临时参数
    delete args;
    //相关变量获取
    MimicProxy *proxy = dynamic_cast<MimicProxy*>(that->service);
    std::shared_ptr<spdlog::logger> logger = that->logger;
    logger->debug("in finish cb, delete {} args tmp ok ", execAddr);
    if(that->judgeDataList.size() == that->addrList.size()) {
        proxy->clearTask(that->sid);
    }
}

int ProxyTask::judgeData(string key) {
    logger->debug("to judge data: {}", key);
    MimicProxy *proxy = dynamic_cast<MimicProxy*>(this->service);
    int execNum = this->addrList.size();

    //只有一个执行体直接返回，并回复前端
    if(this->judgeDataList.size() == 1 && execNum == 1) {
        logger->debug("juse one execcutor in suber");
        this->judgeResult = judgeDataList[key];
        return 1;
    } else if (this->judgeDataList.size() == 1) {
        logger->debug("fisrt resp msg come");
        //第一个执行体来不裁决放入group1
        vector<string> keys;
        keys.emplace_back(key);
        //放入Judggroup
        judgeGroup.emplace(judgeDataList[key], std::move(keys));
    } else {
        //两个以上开始对比
        LeftJudgeDada_t *currentData = judgeDataList[key];
        bool isSameIngroup = false;
        for(auto iter = judgeGroup.begin(); iter != judgeGroup.end(); iter++) {
            int ret = cmpJudgeData(iter->first, iter->second, currentData);
            //对比成功相同
            if (ret == 1) {
                iter->second.emplace_back(key);
                isSameIngroup = true;
                break;
            }
        }

        if (!isSameIngroup) {
            logger->debug("insert new judge group");
            vector<string> keys;
            keys.emplace_back(key);
            //放入Judggroup
            judgeGroup.emplace(judgeDataList[key], std::move(keys));
            //补充新增分组对比信息
            for(auto iter = judgeGroup.begin(); iter != judgeGroup.end(); iter++) {
                if(iter->first->key == key) continue;
                //从其它对比过的分组中获取新分组对比信息
                for(auto element : cmpRetRecord[iter->first->key]) {
                    if(element.judgeDataKey == key) {
                        string error_reason = element.error_reason;
                        string error_detail = element.error_detail;
                        recordCmpErrorRet(judgeDataList[key], iter->first, error_reason, error_detail);
                        break;
                    }
                }
            }
        }
        
        //大数裁决
        for(auto ele : judgeGroup) {
            logger->debug("second szie:{}, ceil: {}, result: {}, group key: {}", ele.second.size(), ceil(execNum / 2.0), judgeResult == NULL ? "" : judgeResult->key, ele.first->key);
            if(ele.second.size() >= ceil(execNum / 2.0) && judgeResult == NULL) {
                //减少返回504的情况
                if(ele.first->respStatus != 504) {
                    logger->debug("compare result come, majourity rule");
                    this->judgeResult = ele.first;
                    this->retGroupKey = ele.first->key;
                    judgeType = "MAJORITY";
                    return 1;
                } else {
                    if(judgeDataList.size() == execNum) {
                        logger->debug("compare result come, majourity rule, but it is 504");
                        for(auto itemFind : judgeGroup) {
                            if(itemFind.first->respStatus >= 200 && itemFind.first->respStatus < 300) {
                                this->judgeResult = itemFind.first;
                                this->retGroupKey = itemFind.first->key;
                                judgeType = "MAJORITY";
                                return 1;
                            }
                        }

                        for(auto itemFind : judgeGroup) {
                            if(itemFind.first->respStatus != 504) {
                                this->judgeResult = itemFind.first;
                                this->retGroupKey = itemFind.first->key;
                                judgeType = "MAJORITY";
                                return 1;
                            }
                        }

                        this->judgeResult = ele.first;
                        this->retGroupKey = ele.first->key;
                        judgeType = "MAJORITY";
                        return 1;
                    }
                }
            }
        }
        //时间或者置信度裁决
        if(judgeResult == NULL && judgeDataList.size() == execNum) {
            logger->debug("compare result come, time rule, weight");
            LeftJudgeDada_t *timeorWeightRet = timeOrWeightJudge();
            //减少返回504的情况
            if(timeorWeightRet->respStatus == 504) {
                logger->debug("compare result come, time rule, weight, ret is 504");
                for(auto itemFind : judgeGroup) {
                    if(itemFind.first->respStatus >= 200 && itemFind.first->respStatus < 300) {
                        this->judgeResult = itemFind.first;
                        this->retGroupKey = itemFind.first->key;
                        judgeType = "TIME_OR_WEIGHT";
                        return 1;
                    }
                }

                for(auto itemFind : judgeGroup) {
                    if(itemFind.first->respStatus != 504) {
                        this->judgeResult = itemFind.first;
                        this->retGroupKey = itemFind.first->key;
                        judgeType = "TIME_OR_WEIGHT";
                        return 1;
                    }
                }
            }

            this->judgeResult = timeorWeightRet;
            logger->debug("time rule ret: {}", this->judgeResult->key);
            this->retGroupKey = judgeResult->key;
            judgeType = "TIME_OR_WEIGHT";
            return 1;
        }
    }
    return 0;
}

void ProxyTask::recordCmpErrorRet(LeftJudgeDada_t *gorupData, LeftJudgeDada_t *comData, string error_reason, string error_detail) {
    logger->debug("record cmp error ret: {}", error_reason);
    if(cmpRetRecord.find(gorupData->key) == cmpRetRecord.end()) {
        vector<judgeDiffRecord_t> recordList;
        judgeDiffRecord_t record = {
            comData->key,
            error_reason,
            error_detail
        };
        recordList.emplace_back(std::move(record));
        cmpRetRecord.emplace(gorupData->key, std::move(recordList));
    } else {
        judgeDiffRecord_t record = {
            comData->key,
            error_reason,
            error_detail
        };
        cmpRetRecord[gorupData->key].emplace_back(std::move(record));
    }
}

int ProxyTask::cmpJudgeData(LeftJudgeDada_t *gorupData, vector<string>& groupkeys, LeftJudgeDada_t *comData) {
    string keys;
    for(auto item : groupkeys) {
        keys = keys + item + ", ";
    }
    MimicProxy *proxy = dynamic_cast<MimicProxy*>(this->service);
    Json::Reader reader;
    Json::Value root;
    Json::Value item;
    Json::Value groupParams;
    Json::Value comParams;
    Json::Reader groupReader;
    Json::Reader comReader;
    string error_reason = "";
    string error_detail = "";
    root["channel"] = "proxy";
    root["service"] = proxy->baseInfo->serviceName;
    item["compared_sid"] = gorupData->sid;
    item["comparing_sid"] = comData->sid;
    item["compared_key"] = gorupData->key;
    item["comparing_key"] = comData->key;
    item["compared_urlfull"] = gorupData->urlfull;
    item["comparing_urlfull"] = comData->urlfull;
    item["compared_http_method"] = MimicUtil::getMethonString(gorupData->method);
    item["comparing_http_method"] = MimicUtil::getMethonString(comData->method);
    item["compared_params"] = "";
    item["comparing_params"] = "";
    if (gorupData->params != ""){
        if (groupReader.parse(gorupData->params, groupParams)){
            item["compared_params"] = groupParams;
        }
        else{
            gorupData->params = gorupData->params.substr(1, gorupData->params.size() - 2);
            item["compared_params"] = groupReader.parse(gorupData->params, groupParams)?groupParams:"ERROR";
        }  
    }
    if (comData->params!=""){
        if (comReader.parse(comData->params, comParams)){
            item["comparing_params"] = comParams;
        }
        else{
            comData->params = comData->params.substr(1, comData->params.size() - 2);
            item["comparing_params"] = comReader.parse(comData->params, comParams)?comParams:"ERROR";
        }  
    }
    item["compared_http_contentype"] = gorupData->contentType;
    item["comparing_http_contentype"] = comData->contentType;
    item["compared_http_bodylen"] = gorupData->bodyLen;
    item["comparing_http_bodylen"] = comData->bodyLen;
    item["compared_http_respstatus"] = gorupData->respStatus;
    item["comparing_http_respstatus"] = comData->respStatus;
    
    if(gorupData->respStatus != comData->respStatus) {
        logger->debug("{} is diffent from in group {} status: {}, {}", 
        comData->key, keys, comData->respStatus, gorupData->respStatus);
        string error_reason = "HTTP_RESPONSESTATUS";
        item["error_reason"] = error_reason;
        root["compare_result"].append(item);
        recordCmpErrorRet(gorupData, comData, error_reason, Json::FastWriter().write(root));
        return 0;
    }
    
    if(gorupData->contentType != comData->contentType) {
        logger->debug("{} is diffent from in group {} content-type: {}, {}", 
        comData->key, keys, comData->contentType, gorupData->contentType);
        string error_reason = "CONTENTTYPE";
        item["error_reason"] = error_reason;
        root["compare_result"].append(item);
        recordCmpErrorRet(gorupData, comData, error_reason, Json::FastWriter().write(root));
        return 0;
    }

    if(gorupData->bodyLen == 0 && comData->bodyLen != 0) {
        logger->debug("{} is diffent from in group {} bodylen: {}, {}", 
        comData->key, keys, comData->bodyLen, gorupData->bodyLen);
        string error_reason = "BODYLEN";
        item["error_reason"] = error_reason;
        root["compare_result"].append(item);
        recordCmpErrorRet(gorupData, comData, error_reason, Json::FastWriter().write(root));
        return 0;
    }

    if(gorupData->bodyLen > 0 && comData->bodyLen == 0) {
        logger->debug("{} is diffent from in group {} bodylen: {}, {}", 
        comData->key, keys, comData->bodyLen, gorupData->bodyLen);
        string error_reason = "BODYLEN";
        root["error_reason"] = error_reason;
        root["compare_result"].append(item);
        recordCmpErrorRet(gorupData, comData, error_reason, Json::FastWriter().write(root));
        return 0;
    }

    if(comData->bodyLen > 0 ) {
        logger->debug("content-type: {}", comData->contentType);
        int code = 0;
        if(comData->contentType.find("json") != std::string::npos) {
            string groupDataCompareBody = "";
            string comDataCompareBody = "";
            Json::Value originGroupJsonData = Json::Value::null;
            Json::Value originComJsonData = Json::Value::null;
            clock_t compareStart, compareEnd;
            compareStart = clock();
            std::tie(code, groupDataCompareBody, comDataCompareBody, originGroupJsonData, originComJsonData) = comJsonBody(gorupData->body, comData->body);
            compareEnd = clock();
            item["compare_spend_time"] = double(compareEnd-compareStart)/CLOCKS_PER_SEC;
            if (code == 100){
                logger->debug("cmp json body success, {}, {}", gorupData->key, comData->key);
                return 1;
            } 
            else if (code == 0){
                logger->debug("{} body is not same group {}", comData->key, keys);
                error_reason = "BODYVALUE";
                item["compared_body_diff_value"] = groupDataCompareBody;
                item["comparing_body_diff_value"] = comDataCompareBody;
                item["compared_origin_http_body"] = originGroupJsonData;
                item["comparing_origin_http_body"] = originComJsonData;
            }
            else if (code == 99) error_reason = "BODYRESOLUTION";
            else error_reason = "UNKOWN";
            
            item["error_reason"] = error_reason;
            root["compare_result"].append(item);
            recordCmpErrorRet(gorupData, comData, error_reason, Json::FastWriter().write(root));
            return 0;
        } else {
            code = comTxtBody(gorupData->body, comData->body);
            if(code != 1) {
                logger->debug("{} body is not same group {}", comData->key, keys);
                string error_reason = "BODYVALUE";
                item["error_reason"] = error_reason;
                root["compare_result"].append(item);
                recordCmpErrorRet(gorupData, comData, error_reason, Json::FastWriter().write(root));
                return 0;
            } else {
                logger->debug("cmp txt body success, {}, {}", gorupData->key, comData->key);
            }
        }
    }
    return 1;
}

string ProxyTask::filterBody(string body){
    string filteredBody = std::regex_replace(std::regex_replace(std::regex_replace(std::regex_replace(std::regex_replace(std::regex_replace(std::regex_replace(std::regex_replace(std::regex_replace(std::regex_replace(std::regex_replace(std::regex_replace(
            body, 
            std::regex("\\t\\n|\\r|\\n|\\t|\\\\t|\\\\r|\\\\n|\\\\t\\\\n|\\\\"), ""),//匹配任意tab，换行，制表，斜杠，去除这些符号
            std::regex("File \""), "File "), //匹配Traceback中的File行，去除File行右边的引号  "Traceback (most recent call last):\n"
                                            //                                              "  File \"example.py\", line 10, in <module>\n"
                                            //                                              "    print(\"Hello, World!\")\n"
                                            //                                              "NameError: name 'print' is not defined\n";
            std::regex("\", line"), ", line"),//匹配Traceback中的line行，去除line行左边的引号 "Traceback (most recent call last):\n"
                                            //                                              "  File \"example.py\", line 10, in <module>\n"
                                            //                                              "    print(\"Hello, World!\")\n"
                                            //                                              "NameError: name 'print' is not defined\n";
            std::regex("\'\\s*\\}"), ""), //匹配'}括起来的内容,且'和}中间可能有内容 "{\"msg\":\"504-{'request_msg': 'Gateway Time Out' }\"}等
            std::regex("\\{\\s*\'"), ""), //匹配{'的内容,且{和'中间可能有内容 "{\"msg\":\"504-{'request_msg': 'Gateway Time Out' }\"等
            std::regex("\'\\s*\\:\\s*\'"), ":"),//匹配':'的内容，且'和:和'中间有内容 "{\"msg\":\"504-{'request_msg': 'Gateway Time Out' }\"等
            std::regex("504-\\{\"(request_msg)\":\"(Gateway Time Out)\"\\}"), "$1:$2"), //匹配自定义的超时信息带有的引号 {"msg": "Response Error: 504-{\"request_msg\":\"Gateway Time Out\"}"}等
            std::regex("\\([0-9]*, \"(.*' \\(.*\\))\"\\)"), "$1"),//匹配{"msg": "(2003, \"Can't connect to MySQL server on '192.168.232.107' (timed out)\")"}等
            std::regex("\"\\{"), "{"), //匹配"{的内容，去除二次转换非整体转换json字符串导致{外有引号 {"vif_details\": \"{\\\"port_filter\\\": true}\"}等
            std::regex("\\}\""), "}"),//匹配}"的内容，去除二次转换非整体转换json字符串导致}外有引号 {"vif_details\": \"{\\\"port_filter\\\": true}\"}等
            std::regex("\"\\["), "["),//匹配"[的内容，去除二次转换非整体转换json字符串导致[外有引号 {"vif_details\": \"{\\\"port_filter\\\": true}\"}等
            std::regex("\\]\""), "]");//匹配]"的内容，去除二次转换非整体转换json字符串导致]外有引号 {"vif_details\": \"{\\\"port_filter\\\": true}\"}等
    return filteredBody;
}

std::tuple<int, string, string, Json::Value, Json::Value> ProxyTask::comJsonBody(string groupBody, string comBody) {
    logger->debug("proxy comJsonBody start");

    Json::Value groupValue;
    Json::Value comValue;
    Json::CharReaderBuilder groupBuilder;
    Json::CharReaderBuilder comBuilder;

    Json::CharReader *groupReader = groupBuilder.newCharReader();
    Json::CharReader *comReader = comBuilder.newCharReader();

    JSONCPP_STRING groupErr;
    JSONCPP_STRING comErr;

    bool isValidGroupData = groupReader->parse(groupBody.c_str(), groupBody.c_str() + groupBody.length(), &groupValue, &groupErr);
    delete groupReader;
    if (!isValidGroupData) {
        logger->debug("Failed to parse group JSON string: body:{} error: {}", groupBody, groupErr);
    }

    bool isValidComData = comReader->parse(comBody.c_str(), comBody.c_str() + comBody.length(), &comValue, &comErr);
    delete comReader;
    if (!isValidComData ) {
        logger->debug("Failed to parse com JSON string: body:{} error: {}", comBody, comErr);
    }

    if (!isValidGroupData || !isValidComData){
        return std::make_tuple(99, 
        !isValidGroupData ? "[PROXY]COMPARED HTTP BODY INVALID JSON" : "", 
        !isValidComData ? "[PROXY]COMPARING HTTP BODY INVALID JSON" : "",
        !isValidGroupData ? Json::Value::null : groupValue,
        !isValidComData ? Json::Value::null : comValue);
    }

    std::vector<string> flatGroupBody;
    std::vector<string> flatComBody;
    std::vector<string> GroupDiffCom;
    std::vector<string> ComDiffGroup;
    //转换json为扁平化json字符串
    flatJson(groupValue, "", "", flatGroupBody, *ignoreKeys, *ignoreKeys);
    flatJson(comValue, "", "", flatComBody, *ignoreKeys, *ignoreKeys);
    int flatGroupBodySize = flatGroupBody.size();
    int flatComBodySize = flatComBody.size();
    //排序扁平化json字符串
    std::sort(std::begin(flatGroupBody), std::end(flatGroupBody));
    std::sort(std::begin(flatComBody), std::end(flatComBody));
    //相互求差
    std::set_difference(flatComBody.begin(), flatComBody.end(), flatGroupBody.begin(), flatGroupBody.end(), std::inserter(ComDiffGroup, ComDiffGroup.begin()));
    std::set_difference(flatGroupBody.begin(), flatGroupBody.end(), flatComBody.begin(), flatComBody.end(), std::inserter(GroupDiffCom, GroupDiffCom.begin()));
    //body长度相同 v1与v2不存在差别 v2与v1不存在差别
    if (flatGroupBodySize == flatComBodySize  && GroupDiffCom.size() ==0 && ComDiffGroup.size() == 0){
        return make_tuple(100, "", "", groupValue, comValue);
    }

    std::stringstream GroupDiffComSS;
    std::stringstream ComDiffGroupSS;

    for (auto it = GroupDiffCom.begin(); it != GroupDiffCom.end(); it++)    {
        if (it != GroupDiffCom.begin()) {
            GroupDiffComSS << ",";
        }
        GroupDiffComSS << *it;
    }
    for (auto it = ComDiffGroup.begin(); it != ComDiffGroup.end(); it++)    {
        if (it != ComDiffGroup.begin()) {
            ComDiffGroupSS << ",";
        }
        ComDiffGroupSS << *it;
    }
    logger->debug("proxy comJsonBody end");
    return make_tuple(0, GroupDiffComSS.str(), ComDiffGroupSS.str(), groupValue, comValue);
}


void ProxyTask::flatJson(const Json::Value& root, const string& key,const string& jsonType, std::vector<string>& flatted, 
vector<string> &ignoreAccurateKeys, vector<string> &ignoreFuzzyKeys, const string &keySeparator, const string &valueSeparator, const string &typeSeparator, const string &typeGroupSeparator) {
    //itr.key().isNumeric()三元操作符是为了去掉数组里的index，有index就会导致顺序对比问题
    //key.size()三元操作是为了去除开头的分隔符
    for (auto itr = root.begin(); itr != root.end(); itr++)
    {   
        //if contains fuzzy ignore keys , ignore it
        if (find(ignoreFuzzyKeys.begin(), ignoreFuzzyKeys.end(), itr.key().asString()) != ignoreFuzzyKeys.end()) continue;
        //if contains accurate ignore keys , ignore it
        //\b avoid meeting the situation that everything is filtered when ignorekey vector is empty
        if (find(ignoreAccurateKeys.begin(), ignoreAccurateKeys.end(), 
        (itr.key().isNumeric() ? "\b" : (key.size() ?(key + keySeparator + itr.key().asString()) : itr.key().asString()) )
        ) != ignoreAccurateKeys.end()) continue;
        switch (itr->type()){
            case Json::ValueType::objectValue:  
                flatJson(*itr, 
                (itr.key().isNumeric() ? "" : (key.size() ? ( key + keySeparator + itr.key().asString()) : itr.key().asString()) ), 
                (jsonType.size() ? jsonType + std::to_string(itr->type()) : itr.key().isNumeric() ? std::to_string(Json::ValueType::arrayValue) + 
                std::to_string(itr->type()) : std::to_string(Json::ValueType::objectValue) + typeGroupSeparator + std::to_string(itr->type())) + typeGroupSeparator, 
                flatted, ignoreAccurateKeys, ignoreFuzzyKeys, keySeparator, valueSeparator, typeSeparator, typeGroupSeparator);
                break;
            case Json::ValueType::arrayValue:
                for(const auto& ele : *itr){
                    if (ele.isObject()){
                        flatJson(ele, 
                        (key.size() ? (!itr.key().isNumeric() ? (key + keySeparator + itr.key().asString()) : key) : itr.key().asString()) , 
                        (jsonType.size() ? (jsonType + std::to_string(itr->type()) + std::to_string(ele.type()))  : (itr.key().isNumeric() ? 
                        (std::to_string(Json::ValueType::arrayValue) + typeGroupSeparator + std::to_string(itr->type())  + typeGroupSeparator+ std::to_string(ele.type())) : 
                        (std::to_string(Json::ValueType::objectValue) + typeGroupSeparator+ std::to_string(itr->type()) + std::to_string(ele.type())))
                        ) + typeGroupSeparator, 
                        flatted, ignoreAccurateKeys, ignoreFuzzyKeys, keySeparator, valueSeparator, typeSeparator, typeGroupSeparator);
                    }
                    if (ele.isArray()){
                        flatJson(ele, 
                        (key.size() ? (!itr.key().isNumeric() ? (key + keySeparator + itr.key().asString()) : key) : itr.key().asString()) , 
                        (jsonType.size() ? (jsonType + std::to_string(itr->type()) + std::to_string(ele.type()))  : (itr.key().isNumeric() ? 
                        (std::to_string(Json::ValueType::arrayValue) + std::to_string(itr->type()) + std::to_string(ele.type())) : 
                        (std::to_string(Json::ValueType::objectValue) + std::to_string(itr->type()) + std::to_string(ele.type())))
                        ), flatted, ignoreAccurateKeys, ignoreFuzzyKeys, keySeparator, valueSeparator, typeSeparator, typeGroupSeparator);
                    }
                    if (ele.isString() || ele.isNumeric() || ele.isInt() || ele.isUInt() || ele.isNull() || ele.isBool()){ 
                        flatted.push_back( 
                        (itr.key().isNumeric()?(MimicUtil::isNumber(key)|| key =="" ?"" : (key.size()>0?key+valueSeparator:"")):
                        (key.size() ?  key + keySeparator + itr.key().asString() + valueSeparator: itr.key().asString() + valueSeparator)) + ele.asString() +
                         typeSeparator + (jsonType.size()?jsonType + std::to_string(itr->type()) + 
                        typeGroupSeparator + std::to_string(ele.type()) : itr.key().isNumeric() ? 
                        std::to_string(Json::ValueType::arrayValue) + std::to_string(itr->type())+ typeGroupSeparator+ std::to_string(ele.type()) : 
                        std::to_string(Json::ValueType::objectValue) + std::to_string(itr->type()) + typeGroupSeparator + std::to_string(ele.type())  ));
                    }
                } 
                break;
            default: 
                flatted.push_back((MimicUtil::isNumber(key)?"":key) +  (itr.key().isNumeric() ? "" : (key.size() ? 
                keySeparator + itr.key().asString() : itr.key().asString()) ) + (MimicUtil::isNumber(itr.key().asString())?(MimicUtil::isNumber(key)|| key ==""?"":valueSeparator):(valueSeparator)) + itr->asString() + typeSeparator + 
                (jsonType.size()?jsonType + (itr.key().isNumeric()?typeGroupSeparator:"") + std::to_string(itr->type()) : (MimicUtil::isNumber(itr.key().asString())?std::to_string(Json::ValueType::arrayValue):std::to_string(Json::ValueType::objectValue)) 
                +typeGroupSeparator + std::to_string(itr->type())));
        }
    }
}

int ProxyTask::comTxtBody(string groupBody, string comBody) {
    if(groupBody.empty() && comBody.empty()) return 1;
    if(!groupBody.empty() && comBody.empty()) return 0;
    if(groupBody.empty() && !comBody.empty()) return 0;

    if(groupBody.size() != comBody.size()) return 0;
    if (strcmp(groupBody.c_str(), comBody.c_str()) == 0) {
        logger->debug("Txt body is same len is equal");
        return 1;
    } else {
        logger->debug("Txt body not same len not equal");
        return 0;
    }
}

void ProxyTask::analyseJudgeResult () {
    logger->debug("in analyse");
    if(retGroupKey.empty()) {
        logger->error("ret group key is null");
        return;
    }

    if(judgeType == "MAJORITY") {
        //遍历裁决分组
        for(auto item : judgeGroup) {
            //结果分组不记录错误信息全部为裁决相同
            if (item.first->key == retGroupKey) continue;
            //遍历与裁决结果不同的所有key
            for(auto ele : item.second) {
                //遍历结果分组的异常key的信息,首先从结果分组的异常信息中找
                bool isFinded = false;
                for(auto inner : cmpRetRecord[retGroupKey]) {
                    //在结果分组的异常key中找到
                    if(inner.judgeDataKey == ele) {
                        //记录异常结果
                        judgeDiffRecord_t record = {
                                            ele,
                                            inner.error_reason,
                                            inner.error_detail
                                        };
                        analyseErrorDetailMap[ele] = record;
                        isFinded = true;
                        break;
                    }
                }
                //本组中找与结果分组不同的异常记录
                if(!isFinded) {
                    for (auto element : cmpRetRecord[item.first->key]) {
                        //找到结果分组的key对应的当前异常分组的异常信息
                        if(element.judgeDataKey == retGroupKey) {
                            //记录异常结果
                            judgeDiffRecord_t record = {
                                            ele,
                                            element.error_reason,
                                            element.error_detail
                                        };
                            analyseErrorDetailMap[ele] =  record;
                            isFinded = true;
                            break;
                        }
                    }
                }
                if(!isFinded) {
                    logger->error("can't find {} judge err info", ele);
                }
            }
        }
    } 
    else {
        for(auto item : judgeGroup) {
            //结果分组不记录错误信息全部为裁决相同
            if (item.first->key == retGroupKey) continue;
            //遍历所有key
            for(auto ele : item.second) {
                //本组中找与结果分组不同的异常记录
                for (auto element : cmpRetRecord[item.first->key]) {
                    //找到结果分组的key对应的当前异常分组的异常信息        
                    //记录异常结果
                    judgeDiffRecord_t record = {
                                        ele,
                                        element.error_reason,
                                        element.error_detail
                                    };
                    analyseErrorDetailMap[ele] = record;
                }
            }
        }
    }
    for(auto recor : analyseErrorDetailMap) {
        logger->info("data key: {}, judge error, error_reason: {}, error_detail:{}", recor.first, recor.second.error_reason, recor.second.error_detail);
    }
    sendToSchedule();
}

LeftJudgeDada_t *ProxyTask::timeOrWeightJudge() {
    logger->debug("in time or weight judge");
    if(judgeRule.find("weight") != std::string::npos) {
        int minFailCount = failcountList.begin()->second;
        for(auto item : failcountList) {
            if(item.second < minFailCount){
                minFailCount = item.second;
            }
        }
        map<string, int> equalList;
        for(auto ele : failcountList) {
            if(ele.second == minFailCount){
                equalList.emplace(ele.first, ele.second);
            }
        }
        if(equalList.size() == 1) {
            for(auto it = judgeDataList.begin(); it != judgeDataList.end(); it++) {
                if(it->second->execAddr == equalList.begin()->first){
                    return it->second;
                }
            }
        } else if(equalList.size() > 1) {
            uint64_t minUptime = uptimelist[equalList.begin()->first];
            for(auto element : uptimelist) {
                if(element.second < minUptime) {
                    minUptime = element.second;
                }
            }
            map<string, uint64_t> equalListUptime;
            for(auto iner : uptimelist) {
                if(iner.second == minUptime) {
                    equalListUptime.emplace(iner.first, iner.second);
                }
            }
            if(equalListUptime.size() == 1) {
                for(auto it = judgeDataList.begin(); it != judgeDataList.end(); it++) {
                    if(it->second->execAddr == equalListUptime.begin()->first){
                        return it->second;
                    }
                }
            } else if(equalListUptime.size() > 1) {
                uint64_t seed = MimicUtil::getRandSeed();
                std::srand(seed);
                int index = rand() % equalListUptime.size();
                int number = 0;
                for(auto it = judgeDataList.begin(); it != judgeDataList.end(); it++) {
                    if(number == index) {
                        return it->second;
                    }
                    number++;
                }
            } else {
                logger->error("can't find judge result");
                return judgeDataList.begin()->second;
            }
        } else {
            logger->error("can't find judge result");
            return judgeDataList.begin()->second;
        }
    } else {
        uint64_t minUptime = uptimelist[uptimelist.begin()->first];
        for(auto element : uptimelist) {
            if(element.second < minUptime) {
                minUptime = element.second;
            }
        }
        map<string, uint64_t> equalListUptime;
        for(auto iner : uptimelist) {
            if(iner.second == minUptime) {
                equalListUptime.emplace(iner.first, iner.second);
            }
        }
        if(equalListUptime.size() == 1) {
            for(auto it = judgeDataList.begin(); it != judgeDataList.end(); it++) {
                if(it->second->execAddr == equalListUptime.begin()->first){
                    return it->second;
                }
            }
        } else if(equalListUptime.size() > 1) {
            uint64_t seed = MimicUtil::getRandSeed();
            std::srand(seed);
            int index = rand() % equalListUptime.size();
            int number = 0;
            for(auto it = judgeDataList.begin(); it != judgeDataList.end(); it++) {
                if(number == index) {
                    return it->second;
                }
                number++;
            }
        } else {
            logger->error("can't find judge result");
            return judgeDataList.begin()->second;
        }
    }
}