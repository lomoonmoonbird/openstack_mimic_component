#include <event2/util.h>
#include <event2/http.h>
#include<sys/socket.h>
#include<regex>
#include <algorithm>
#include <sstream>
#include <time.h>
#include "mimic_task.hpp"
#include "mimic_judge.hpp"
#include "mimic_jthread.hpp"

JudgeTask::JudgeTask(string sid, evbase_t *evbase, void *jthread, int execNum):sid(sid),
evbase(evbase),jthread(jthread),execNum(execNum) {
    this->mutexRightJudge = PTHREAD_MUTEX_INITIALIZER;
    MimicJthread *jthr = (MimicJthread *)jthread;
    this->logger = jthr->logger;
    judgeResult = NULL;
    MimicJudge *judge = dynamic_cast<MimicJudge*>(jthr->service);
    ignoreKeys = judge->ignoreKeys;

    memset(&rTimeout,0x00,sizeof(rTimeout));
    memset(&wTimeout,0x00,sizeof(wTimeout));
    this->rTimeout.tv_sec = judge->baseInfo->timeout;
    this->wTimeout.tv_sec = judge->baseInfo->timeout;

    backendRespCome = false;
    isClean = false;
    tcpsendend = false;
    isRespStared = false;
    judgeNum = 0;
    judgeNumCtr = 0;
}

JudgeTask::~JudgeTask(){}

void JudgeTask::judgeTimeoutCb(evutil_socket_t fd, short event, void *arg) {
    JudgeTask *that = (JudgeTask *)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    logger->debug("in judge timout cb: sid: {}", that->sid);

    vector<string> notComeJudgeData;
    vector<string> comedJudgeData;
    for(auto element : that->judgeDataList) {
        comedJudgeData.emplace_back(element.second->execAddr);
    }
    if(that->judgeDataList.size() != that->serverarray.size()) {
        logger->debug("exist some timeout judge req, sid: {}", that->sid);
        for(auto item : that->serverarray) {
            if(find(comedJudgeData.begin(), comedJudgeData.end(), item) == comedJudgeData.end()) {
                notComeJudgeData.emplace_back(item);
            }
        }
    } else {
        logger->debug("not exist some timeout judge req, sid: {}", that->sid);
        return;
    }

    int coutp = comedJudgeData.size();
    logger->debug("come data size: {}, sid: {}", coutp, that->sid);
    for(auto item1 : notComeJudgeData) {
        coutp += 1;
        RightJudgeDada_t *judgedata = new RightJudgeDada_t;
        judgedata->urlfull = "/default_timeout";
        judgedata->urlall = "/default_timeout";
        judgedata->urlraw = "";
        judgedata->reqHeaders["Content-Type"] = "application/json";
        judgedata->method = htp_method_GET;
        judgedata->contentType = "application/json";
        judgedata->body = "{\"request_msg\":\"Gateway Time Out\"}";
        judgedata->bodyLen = strlen("{\"request_msg\":\"Gateway Time Out\"}");
        judgedata->group = -1;
        judgedata->key = that->sid + "-" + item1 + "-" + std::to_string(coutp);
        judgedata->execAddr = item1;
        judgedata->sid = that->sid;

        logger->debug("sid: {}, key: {}, timeout to process", that->sid, that->sid + "-" + item1 + "-" + std::to_string(coutp));
        that->processJudgeData(judgedata, that->sid + "-" + item1 + "-" + std::to_string(coutp));
        if(coutp >= that->serverarray.size()) {
            return;
        }
    }
}

void JudgeTask::responseToFrondReq(bool isFromProcess) {
    //加锁控制
    pthread_mutex_lock(&(this->mutexRightJudge));
    if(isFromProcess) judgeNum += 1;
    else isRespStared = true;
    if(!backendRespCome) {
        logger->debug("has not come backend resp, sid: {}", sid);
        pthread_mutex_unlock(&(this->mutexRightJudge));
        return;
    }
    MimicJthread *jthr = (MimicJthread *)jthread;
    if(jthr->taskList.find(sid) == jthr->taskList.end()) {
        logger->debug("maybe clear over, sid: {}", sid);
        pthread_mutex_unlock(&(this->mutexRightJudge));
        return;
    }
    //打印日志
    string headersresp;
    for(auto ele : backendRespHeaders) {
        headersresp += "\n" + ele.first + ": " + ele.second;
    }
    logger->debug("headersresp: {}", headersresp);

    //循环回复数据
    for(auto iter = judgeDataList.begin(); iter != judgeDataList.end(); iter++) {
        //超时处理
        if(iter->second->urlfull == "/default_timeout") {
            logger->debug("this req is timeout struct, dont to reply, sid: {}", sid);
            continue;
        }
        //是否已经回复
        if(respIsOk.find(iter->second->reqHeaders["Mimic-Host"]) != respIsOk.end() && 
        respIsOk[iter->second->reqHeaders["Mimic-Host"]] == "ok") {
            logger->debug("already resp: {}, sid: {}", iter->second->reqHeaders["Mimic-Host"], sid);
            continue;
        }
        //给backend变量赋值
        backendResp_t *backendResp = new backendResp_t;
        backendResp->backendRespHeaders = backendRespHeaders;
        backendResp->backendRespStatus = backendRespStatus;
        backendResp->backendRespBody = backendRespBody;
        backendResp->backendRespBodyLen = backendRespBodyLen;
        //增加sn
        logger->debug("insert sn and sid to backendresp, sn: {}, sid: {}", iter->second->sn, iter->second->sid);
        backendResp->backendRespHeaders["mimic-sn"] = std::to_string(iter->second->sn);
        backendResp->backendRespHeaders["sid"] = iter->second->sid;

        //向收发线程发送数据
        backendResp_t **tp = &backendResp;
        int rc = write(iter->second->replyfd, tp, sizeof(backendResp_t *));
        if(rc > 0) {
            logger->debug("send to jctl ok, exec: {}, sid: {}", iter->second->reqHeaders["Mimic-Host"], iter->second->sid);
        } else {
            logger->error("send to jctl fail, exec: {}, sid: {}", iter->second->reqHeaders["Mimic-Host"], iter->second->sid);
        }
        respIsOk[iter->second->reqHeaders["Mimic-Host"]] = "ok";
    }
    logger->debug("has resp to frond all ok , req size: {}, judgenum: {}, sid: {}", respIsOk.size(), judgeNum, sid);
    //是否已经全部回复完成
    bool isallresp = true;
    for(auto item : respIsOk) {
        logger->debug("sid: {}, respisok_key: {}, respisokval: {}", sid, item.first, item.second);
        if(item.second != "ok") {
            isallresp = false;
            break;
        }
    }
    logger->debug("isallresp: {}, isRespStarted: {}, judgenum: {}, sid: {}", isallresp, isRespStared, judgeNum, sid);
    //是否清理数据
    bool istoClean = false;
    if(judgeNum == serverarray.size() && isallresp && isRespStared) istoClean = true;
    pthread_mutex_unlock(&(this->mutexRightJudge));
    //分析裁决日志并清理
    if(istoClean) {
        analyseJudgeResult();
        clearNewData();
        jthr->clearTask(sid);
    }
}

void JudgeTask::gotoErrBackendResp() {
    backendRespHeaders["Content-Type"] = "application/json";
    backendRespStatus = EVHTP_RES_GWTIMEOUT;
    backendRespBody = "{\"request_msg\":\"Gateway Time Out\"}"; 
    backendRespBodyLen = strlen(backendRespBody.c_str());
    backendRespCome = true;
    if(suber->isHorizon) {
        logger->debug("is horizon should to resp to frond, sid:{}", sid);
        responseToFrondReq(false);
    } else {
        logger->debug("is not horizon judge result error dont send to backend. sid: {}", sid);
    }
}

int JudgeTask::fillUpHeadesMap(evhtp_kv_t * kvobj, void * arg) {
    map<string, string> *headersMap = (map<string, string> *)arg;
    headersMap->emplace(kvobj->key, kvobj->val);
    return 0;
}

void JudgeTask::backendRespCb(evhtp_request_t * req, void * arg) {
    JudgeTask *that = (JudgeTask*)arg;
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
            logger->warn("body len not same, sid: {}", that->sid);
        }
    }
    logger->debug("backend resp status: {}, body len: {}, sid: {}, body: {}", status, bodyLen, that->sid, body == NULL ? "" : body);
    //赋值
    that->backendRespStatus = status;
    that->backendRespBody = body == NULL ? "" : body;
    that->backendRespBodyLen = bodyLen;
    if(body != NULL) free(body);
    evhtp_kvs_for_each(req->headers_in, JudgeTask::fillUpHeadesMap, &(that->backendRespHeaders));

    that->backendRespCome = true;

    evhtp_request_free(req);
    if(that->suber->isHorizon) {
        logger->debug("is horizon should to resp to frond");
        that->responseToFrondReq(false);
    } else {
        logger->debug("is not horizon didnt resp");
    }
    //释放资源
    // shutdown(req->conn->sock,SHUT_RDWR);
    // close(req->conn->sock);
}

evhtp_res JudgeTask::backendRespErrCb(evhtp_request_t * req, evhtp_error_flags errtype, void * arg){
    JudgeTask *that = (JudgeTask*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    logger->debug("backedn err resp cb");
    //返回状态码
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
                logger->warn("body len not same, sid: {}", that->sid);
            }
        }
        logger->debug("backend resp status: {}, body len: {}, sid: {}", status, bodyLen, that->sid);
        //赋值
        that->backendRespStatus = status;
        that->backendRespBody = body == NULL ? "" : body;
        that->backendRespBodyLen = bodyLen;
        if(body != NULL) free(body);
        evhtp_kvs_for_each(req->headers_in, JudgeTask::fillUpHeadesMap, &(that->backendRespHeaders));

        that->backendRespCome = true;

        evhtp_request_free(req);
        if(that->suber->isHorizon) {
            logger->debug("is horizon should to resp to frond");
            that->responseToFrondReq(false);
        } else {
            logger->debug("is not horizon didnt resp");
        }
    } else {
        logger->debug("in backend err resp cb, sid:{}", that->sid);
        evhtp_request_free(req);
        that->gotoErrBackendResp();
        // shutdown(req->conn->sock,SHUT_RDWR);
    }
}

void JudgeTask::sendToBackend(string addr) {
    logger->debug("in send to backend, sid: {}", sid);
    if(judgeResult->reqHeaders.find("backend") == judgeResult->reqHeaders.end()) {
        logger->debug("judge err, maybe req timeout, sid: {}", sid);
        gotoErrBackendResp();
        return;
    }
    if(suber->isHorizon) {
        struct evhttp_uri *http_uri = evhttp_uri_parse(judgeResult->reqHeaders["backend"].c_str());
        string host = evhttp_uri_get_host(http_uri);
        int port = evhttp_uri_get_port(http_uri);
        char uri[256];
        const char *path = evhttp_uri_get_path(http_uri);
        if (strlen(path) == 0) path = "/";
        const char *query = evhttp_uri_get_query(http_uri);
        if (query == NULL) {
            snprintf(uri, sizeof(uri) - 1, "%s", path);
        } else {
            snprintf(uri, sizeof(uri) - 1, "%s?%s", path, query);
        }
        uri[sizeof(uri) - 1] = '\0';
        if (http_uri) evhttp_uri_free(http_uri);
        logger->debug("sid: {}, backend uri: {}", sid, uri);

        //创建连接对象
        evhtp_connection_t * conn=evhtp_connection_new(evbase, host.c_str(), port);
        if(conn==NULL) {
            logger->error("evhtp conn fail");
            gotoErrBackendResp();
            return;
        }
        evhtp_connection_set_timeouts(conn, &(rTimeout), &(wTimeout));
        //创建请求对象
        evhtp_request_t *request = evhtp_request_new(JudgeTask::backendRespCb, this);
        if(!request) {
            gotoErrBackendResp();
            return;
        }
        request->method = judgeResult->method;
        //设置回调
        evhtp_request_set_hook(request, evhtp_hook_on_error,(evhtp_hook)JudgeTask::backendRespErrCb, this);
        //增加请求头
        for(auto item : judgeResult->reqHeaders) {
            if(item.first == "mimic-sn") continue;
            evhtp_headers_add_header(request->headers_out,evhtp_header_new(item.first.c_str(), item.second.c_str(), 1, 1));
        }
        //增加请求体
        if(judgeResult->bodyLen > 0) evbuffer_add(request->buffer_out, judgeResult->body.c_str(), strlen(judgeResult->body.c_str())); 
        //发起请求
        if(evhtp_make_request(conn,request, request->method, uri) == 0) {
            logger->debug("send to backend : {} , uri: {} success, sid: {}", host + ":" + std::to_string(port), uri, sid);
        } else {
            logger->debug("send to backend : {} , uri: {} fail, sid: {}", host + ":" + std::to_string(port), uri, sid);
            gotoErrBackendResp();
        }
    } else if(suber->isController) {
        //云控直接把tcp请求发送body数据
        logger->debug("backend ip host: {}", judgeResult->reqHeaders["backend"]);
        string backendaddr = judgeResult->reqHeaders["backend"];
        regex pattern("(.*):(.*)");
        smatch results;
        string ip, port;
        if (regex_match(backendaddr, results, pattern)) {
            ip = results[1];
            port = results[2];
        } else {
            logger->error("parse ip port fail, dont send to backend: {}", backendaddr);
            return;
        }
        logger->debug("send to backend");
        
        MimicJthread *jthr = (MimicJthread *)jthread;
        MimicJudge *judge = dynamic_cast<MimicJudge*>(jthr->service);
        string sendbody = judgeResult->body + "\r\n\r\n";
        MimicTcpClient *client = new MimicTcpClient(ip, atoi(port.c_str()), sendbody, evbase, judge, sid);
        client->tcpRequest();
        /**
        string sendbody = judgeResult->body + "\r\n\r\n\r\n";
        bool ret = MimicUtil::tcpSend(ip, atoi(port.c_str()), sendbody, false, sid, logger);
        if(!ret) logger->error("send to backend fail, sid: {}", sid);
        **/
        tcpsendend = true;
        logger->debug("send to backend end");
    }
}

void JudgeTask::processJudgeData(RightJudgeDada_t *judgedata, string key) {
    logger->debug("in process judge data, sid: {}", sid);
    MimicJthread *jthr = (MimicJthread *)jthread;
    MimicJudge *judge = dynamic_cast<MimicJudge*>(jthr->service);

    //加入裁决数据
    pthread_mutex_lock(&(this->mutexRightJudge));
    judgeDataList.emplace(key, judgedata);
    //第一个数据来时增加裁决超时定时器
    if(judgeDataList.size() == 1) {
        firstReqData = judgedata;
        logger->debug("first req in, init judge timout event, sid: {}", sid);
        memset(&ev_tv,0x00,sizeof(ev_tv)); 
        ev_tv.tv_sec = judge->baseInfo->timeout;
        time_out_ev = event_new(evbase, -1, EV_WRITE|EV_READ, JudgeTask::judgeTimeoutCb, this);
        event_add(time_out_ev, &ev_tv);
    }
    //是否有裁决结果
    int ret = judgeData(key);
    pthread_mutex_unlock(&(this->mutexRightJudge));
    //有裁决结果发送给后端地址
    if (ret == 1) sendToBackend(judgedata->urlfull == "/default_timeout" ? firstReqData->execAddr : judgedata->execAddr);
    pthread_mutex_lock(&(this->mutexRightJudge));
    judgeNumCtr += 1;
    if(judge->baseInfo->serviceName.find("horizon") == std::string::npos) {
        if(judgeNumCtr < serverarray.size()) {
            pthread_mutex_unlock(&(this->mutexRightJudge));
            logger->debug("prev return avoid double free, num: {}", judgeNumCtr);
            return;
        }
    }
    pthread_mutex_unlock(&(this->mutexRightJudge));
    //如有结果回复数据
    if(judge->baseInfo->serviceName.find("horizon") != std::string::npos) {
        //double free todo
        responseToFrondReq(true);
    } else {
        if(judgeNumCtr == serverarray.size() && tcpsendend) {
            analyseJudgeResult();
            clearNewData();
            jthr->clearTask(sid);
        }
    }
}

int JudgeTask::judgeData(string key) {
    logger->debug("to judge data: {}", key);
    //只有一个执行体直接返回，并回复前端
    if(this->judgeDataList.size() == 1 && execNum == 1) {
        logger->debug("juse one execcutor in server array, key: {}", key);
        this->judgeResult = judgeDataList[key];
        return 1;
    } else if (this->judgeDataList.size() == 1) {
        logger->debug("fisrt req msg come, key: {}", key);
        //第一个执行体来不裁决放入group1
        vector<string> keys;
        keys.emplace_back(key);
        //放入Judggroup
        judgeGroup.emplace(judgeDataList[key], std::move(keys));
    } else {
        //两个以上开始对比
        RightJudgeDada_t *currentData = judgeDataList[key];
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
            logger->debug("insert new judge group, key: {}", key);
            vector<string> keys;
            keys.emplace_back(key);
            //放入Judggroup
            judgeGroup.emplace(judgeDataList[key], std::move(keys));
            //补充对比信息
            for(auto iter = judgeGroup.begin(); iter != judgeGroup.end(); iter++) {
                //修复cmprecord 相关内容为空情况
                if(iter->first->key == key) continue;
                //从其它对比过的分组中获取当前分组的对比信息
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
            logger->debug("second szie:{}, result: {}, group key: {}", ele.second.size(), judgeResult == NULL ? "" : judgeResult->key, ele.first->key);
            if(ele.second.size() >= ceil(execNum / 2.0) && judgeResult == NULL) {
                logger->debug("compare result come, majourity rule, sid: {}", sid);
                //如果是大数裁决是超时的结果则取第一个数据为结果减少页面超时的情况
                if(ele.first->urlfull == "/default_timeout") {
                    this->judgeResult = firstReqData;
                    this->retGroupKey = firstReqData->key;
                    this->urlfull = firstReqData->urlfull;
                } else {
                    this->judgeResult = ele.first;
                    this->retGroupKey = ele.first->key;
                    this->urlfull = ele.first->urlfull;
                }
                judgeType = "MAJORITY";
                return 1;
            }
        }
        //时间或者置信度裁决
        if(judgeResult == NULL && judgeDataList.size() == execNum) {
            logger->debug("compare result come, time rule, weight, sid: {}", sid);
            this->judgeResult = timeOrWeightJudge();
            if(this->judgeResult->urlfull == "/default_timeout") {
                this->judgeResult = firstReqData;
                this->retGroupKey = firstReqData->key;
                this->urlfull = firstReqData->urlfull;
            } else {
                logger->debug("time rule ret: {}", this->judgeResult->key);
                this->retGroupKey = judgeResult->key;
                this->urlfull = judgeResult->urlfull;
            }
            judgeType = "TIME_OR_WEIGHT";
            return 1;
        }
    }
    return 0;
}

int JudgeTask::cmpJudgeData(RightJudgeDada_t *gorupData, vector<string>& groupkeys, RightJudgeDada_t *comData) {
    string keys;
    for(auto item : groupkeys) {
        keys = keys + item + ", ";
    }

    string error_reason = "";
    string error_detail = "";
    Json::Reader reader;
    Json::Value root;
    Json::Value item;

    root["channel"] = "judge";
    root["service"] = this->suber->service->baseInfo->serviceName;
    item["compared_sid"] = gorupData->sid;
    item["comparing_sid"] = comData->sid;
    item["compared_key"] = gorupData->key;
    item["comparing_key"] = comData->key;
    item["compared_http_method"] = MimicUtil::getMethonString(gorupData->method);
    item["comparing_http_method"] = MimicUtil::getMethonString(comData->method);
    item["compared_urlfull"] = gorupData->urlfull;
    item["comparing_urlfull"] = comData->urlfull;
    item["compared_http_contentype"] = gorupData->contentType;
    item["comparing_http_contentype"] = comData->contentType;
    item["compared_http_bodylen"] = gorupData->bodyLen;
    item["comparing_http_bodylen"] = comData->bodyLen;
    
    if(gorupData->method != comData->method) {
        logger->debug("{} is diffent from in group {} method: {}, {}", 
        comData->key, keys, comData->method, gorupData->method);
        
        if(comData->body.find("Gateway") != std::string::npos) {
            error_reason = "HTTP_METHOD&GATEWAY_TIMEOUT";
        } else {
            error_reason = "HTTP_METHOD";
        }
        item["error_reason"] = error_reason;
        root["compare_result"].append(item);
        recordCmpErrorRet(gorupData, comData, error_reason, Json::FastWriter().write(root));
        return 0;
    }

    if(gorupData->urlfull != comData->urlfull) {
        logger->debug("{} is diffent from in group {} urlfull: {}, {}", 
        comData->key, keys, comData->urlfull, gorupData->urlfull);
        if(comData->body.find("Gateway") != std::string::npos) {
            error_reason = "URL&GATEWAY_TIMEOUT";
        } else {
            error_reason = "URL";
        }
        item["error_reason"] = error_reason;
        root["compare_result"].append(item);
        recordCmpErrorRet(gorupData, comData, error_reason, Json::FastWriter().write(root));
        return 0;
    }
    
    if(gorupData->contentType != comData->contentType) {
        logger->debug("{} is diffent from in group {} content-type: {}, {}", 
        comData->key, keys, comData->contentType, gorupData->contentType);
        error_reason = "CONTENTTYPE";
        item["error_reason"] = error_reason;
        root["compare_result"].append(item);
        recordCmpErrorRet(gorupData, comData, error_reason, Json::FastWriter().write(root));
        return 0;
    }

    if(gorupData->bodyLen == 0 && comData->bodyLen != 0) {
        logger->debug("{} is diffent from in group {} bodylen: {}, {}", 
        comData->key, keys, comData->bodyLen, gorupData->bodyLen);
        error_reason = "BODYLEN";
        item["error_reason"] = error_reason;
        root["compare_result"].append(item);
        recordCmpErrorRet(gorupData, comData, error_reason, Json::FastWriter().write(root));
        return 0;
    }

    if(gorupData->bodyLen > 0 && comData->bodyLen == 0) {
        logger->debug("{} is diffent from in group {} bodylen: {}, {}", 
        comData->key, keys, comData->bodyLen, gorupData->bodyLen);
        error_reason = "BODYLEN";
        item["error_reason"] = error_reason;
        root["compare_result"].append(item);
        recordCmpErrorRet(gorupData, comData, error_reason, Json::FastWriter().write(root));
        return 0;
    }
    if(comData->bodyLen > 0 ) {
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
            if (code == 0){
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

string JudgeTask::filterBody(string body){
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

std::tuple<int, string, string, Json::Value, Json::Value> JudgeTask::comJsonBody(string groupBody, string comBody) {
    logger->debug("judge comJsonBody start");
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
        !isValidGroupData ? "[JUDGE]COMPARED HTTP BODY INVALID JSON" : "", 
        !isValidComData ? "[JUDGE]COMPARING HTTP BODY INVALID JSON" : "",
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
    logger->debug("judge comJsonBody end");
    return make_tuple(0, GroupDiffComSS.str(), ComDiffGroupSS.str(), groupValue, comValue);
}

void JudgeTask::flatJson(const Json::Value& root, const string& key,const string& jsonType, std::vector<string>& flatted, 
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
        (itr.key().isNumeric() ? "\b" : (key.size() ?
         (key + keySeparator + itr.key().asString()) : itr.key().asString()) )) != ignoreAccurateKeys.end()) continue;
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
                (jsonType.size()?jsonType + (itr.key().isNumeric()?typeGroupSeparator:"") + std::to_string(itr->type()) : (MimicUtil::isNumber(itr.key().asString())?std::to_string(Json::ValueType::arrayValue):std::to_string(Json::ValueType::objectValue)) +typeGroupSeparator + 
                std::to_string(itr->type())));
        }
    }
}

int JudgeTask::comTxtBody(string groupBody, string comBody) {
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

void JudgeTask::analyseJudgeResult () {
    logger->debug("in analyse, sid: {}", sid);
    if(retGroupKey.empty()) {
        logger->error("ret group key is null, sid: {}", sid);
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
                        analyseErrorDetailMap[ele] =  record;
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
                            analyseErrorDetailMap[ele] = record;
                            isFinded = true;
                            break;
                        }
                    }
                }
                if(!isFinded) {
                    logger->error("can't find {} judge err info, sid: {}", ele, sid);
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

RightJudgeDada_t *JudgeTask::timeOrWeightJudge() {
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

void JudgeTask::clearNewData() {
    logger->debug("clear data");
    for(auto item : judgeDataList) {
        // if(item.second->bodyLen > 0) {
        //     free(item.second->body.c_str());
        // }
        delete item.second;
    }
}

void JudgeTask::recordJudgeLog(string jsonstr) {
    logger->debug("record judge log: {}", jsonstr);
    MimicJthread *jthr = (MimicJthread *)jthread;
    MimicJudge *judge = dynamic_cast<MimicJudge*>(jthr->service);

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
    for(auto item : serverarray) {
        // logger->debug("item in serverarray: {}, arch: {}, id: {}, sid: {}", item, suber->allExecList[item].arch, suber->allExecList[item].id, sid);
        execids += suber->allExecList[judge->convertFromCincerOrNeutron(item)].arch + "_" + 
        std::to_string(suber->allExecList[judge->convertFromCincerOrNeutron(item)].id) + ",";
    }
    for(auto ele : value["data"]) {
        // logger->debug("ele in json addr: {}, arch: {}, id: {}, sid: {}", ele["addr"].asString(), suber->allExecList[ele["addr"].asString()].arch, suber->allExecList[ele["addr"].asString()].id, sid);
        execerrids += suber->allExecList[ele["addr"].asString()].arch + "_" + 
        std::to_string(suber->allExecList[ele["addr"].asString()].id) + ",";
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
    } else if(suber->isController) {
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

void JudgeTask::sendToSchedule() {
    logger->debug("send to schedule, sid: {}", sid);
    if(analyseErrorDetailMap.empty()) {
        logger->info("no error exec, no need to send schedule and record log, sid: {}", sid);
        return;
    }
    MimicJthread *jthr = (MimicJthread *)jthread;
    MimicJudge *judge = dynamic_cast<MimicJudge*>(jthr->service);
    //record log
    Json::FastWriter writer;
    Json::Value outer;
    Json::Value data;
    Json::Value errorDetailTmp;
    Json::Reader reader;
    outer["mode"] = "report_error";
    for(auto item : analyseErrorDetailMap) {
        Json::Value inner;
        inner["addr"] = judge->convertFromCincerOrNeutron(judgeDataList[item.first]->execAddr);
        inner["msg"] = item.second.error_reason;
        reader.parse( item.second.error_detail.c_str(), errorDetailTmp );
        inner["error_reason"].append(item.second.error_reason);
        inner["error_detail"].append(errorDetailTmp);
        data.append(inner);
    }
    outer["data"] = data;
    string reportStr = writer.write(outer);
    logger->debug("to record log json str: {}, sid: {}", reportStr, sid);
    recordJudgeLog(reportStr);
    //send to schedule
    Json::FastWriter writer1;
    Json::Value outer1;
    Json::Value data1;
    outer1["mode"] = "report_error";
    string newreportstr;
    bool issend = false;
    logger->debug("sid: {}, 1111111111111111111111111111111111111111", sid);
    for(auto logprint : judge->judgeErrNum) {
        logger->debug("first: {}, second: {}", logprint.first, logprint.second);
    }
    for(auto ele : analyseErrorDetailMap) {
        Json::Value inner1;
        logger->debug("ele.first: {}, execaddr: {}", ele.first, judgeDataList[ele.first]->execAddr);
        if(judge->judgeErrNum.find(judgeDataList[ele.first]->execAddr) == judge->judgeErrNum.end()) {
            logger->debug("aaaaaaaaaaaaa");
            judge->judgeErrNum.emplace(judgeDataList[ele.first]->execAddr, 1);
            logger->debug("bbbbbbbbbbbbbbbbbbbbbbbbb");
        } else {
            logger->debug("cccccccccccccccccccccccc");
            if(judge->judgeErrNum[judgeDataList[ele.first]->execAddr] < atoi(suber->judgeErrorCount.c_str())) {
                logger->debug("dddddddddddddddddddd");
                judge->judgeErrNum[judgeDataList[ele.first]->execAddr] += 1;
                logger->debug("eeeeeeeeeeeeeeeeeee");
            } else {
                logger->debug("fffffffffffffffffffffffff");
                judge->judgeErrNum[judgeDataList[ele.first]->execAddr] = 0;
                logger->debug("ggggggggggggggggggggggg");
                inner1["addr"] = judge->convertFromCincerOrNeutron(judgeDataList[ele.first]->execAddr);
                logger->debug("hhhhhhhhhhhhhhhhhhhhhhhhh");
                inner1["msg"] = ele.second.error_reason;;
                logger->debug("iiiiiiiiiiiiiiiiiiiiiiiiiiii");
                data1.append(inner1);
                issend = true;
                logger->debug("jjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjj");
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
            ip = judge->globalConf->schedule_controller_ip;
            port = judge->globalConf->schedule_controller_port;
        } else if(suber->isHorizon) {
            ip = judge->globalConf->schedule_horizon_ip;
            port = judge->globalConf->schedule_horizon_port;
        } else if(suber->isKeystone) {
            ip = judge->globalConf->schedule_keystone_ip;
            port = judge->globalConf->schedule_keystone_port;
        } else {
            logger->error("can't find schedule ip and port ,dont send to schedule, sid: {}", sid);
        }
        logger->debug("sid: {}, 3333333333333333333333333333333333333333333333", sid);
        MimicTcpClient *client = new MimicTcpClient(ip, port, newreportstr, evbase, judge, sid);
        client->tcpRequest();
    }
}

void JudgeTask::recordCmpErrorRet(RightJudgeDada_t *gorupData, RightJudgeDada_t *comData, string error_reason, string error_detail) {
    logger->debug("record cmp error error_reason: {}, error_detail: {}, gkey: {}, ckey: {}", error_reason, error_detail, gorupData->key, comData->key);
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