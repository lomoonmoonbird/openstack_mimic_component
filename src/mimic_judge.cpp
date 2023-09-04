#include <iostream>
#include "mimic_judge.hpp"
#include "mimic_util.hpp"
#include "mimic_suber.hpp"
#include <sys/syscall.h>
#include <sys/types.h>
#include <regex>
#include <iostream>
#include <cmath>
#include <unistd.h>

#include "spdlog/spdlog.h"

MimicJudge::MimicJudge(GlobalInfo_t *globalConf, 
vector<string> *ignoreKeys, ServiceBaseInfo_t *baseInfo)
{
    initProxyJudgeInfo(globalConf, ignoreKeys, baseInfo);
    sn = 0;
}

MimicJudge::~MimicJudge() {
    if(jthreadList.size() > 0) {
        for(auto iter = jthreadList.begin(); iter != jthreadList.end(); iter++){
            if(iter->second != nullptr){
                delete iter->second;
                iter->second = nullptr;
            }
        }
    }

    if(jctlList.size() >0 ) {
        for(auto iter = jctlList.begin(); iter != jctlList.end(); iter++) {
            if((*iter) != nullptr) {
                delete *iter;
                *iter = nullptr;
            }
        }
    }
}

void MimicJudge::sigIntCbFun(int sig, short why, void *arg)
{
    MimicJudge *that = (MimicJudge*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    
    logger->debug("Stop service!");
    if(that->jthreadList.size() > 0) {
        for(auto iter = that->jthreadList.begin(); iter != that->jthreadList.end(); iter++){
            if(iter->second != nullptr){
                iter->second->jthreadStop();
            }
        }
    }
    event_base_loopexit(that->evbase, NULL);
}

void MimicJudge::replyToFront(int fd, short event, void *arg) {
    judgeCtl_t *judgectl = (judgeCtl_t*)arg;
    MimicJudge *that = (MimicJudge *)judgectl->judge;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    logger->debug("in reply cb");

    //接收返回数据 有正常数据与 非正常数据
    char * recvbuf[10]={0};
    int rc = read(fd, recvbuf, sizeof(backendResp_t *));
    backendResp_t **backendRespTpp = ( backendResp_t **)recvbuf;
    //打印指针类型日志
    char logbuf[1024] = {0};
    sprintf(logbuf, "judge_reply_cb[%d][%p][%p]", rc, backendRespTpp, *backendRespTpp);
    logger->debug("backendresp point: {}", logbuf);

    //获取sid
    string sid;
    if((*backendRespTpp)->backendRespHeaders.find("sid") == (*backendRespTpp)->backendRespHeaders.end()) {
        sid = "nosid";
    } else sid = (*backendRespTpp)->backendRespHeaders["sid"];
    //获取sn 从jthread返回的数据必带有sn
    int mimic_sn = atoi((*backendRespTpp)->backendRespHeaders["mimic-sn"].c_str());
    logger->debug("resply mimic sn: {}, sid: {}", mimic_sn, sid);

    //云控不返回数据，走到这里说明有错误发生
    if(that->baseInfo->serviceName.find("controller") != std::string::npos) {
        logger->debug("controller not to resp, in this, it is error in jthread, sid: {}, sn: {}", sid, mimic_sn);
        delete *backendRespTpp;
        return;
    }
    //获取req
    that->lockInPj();
    map<int, evhtp_request_t *>::iterator iter = judgectl->frontReqList.find(mimic_sn);
    if(iter != judgectl->frontReqList.end()) {
        logger->debug("resp to front start, sid: {}", sid);
        //判断连接是否关闭
        if(iter->second->buffer_out == NULL || iter->second->buffer_in == NULL) {
            logger->debug("connection close,may timeout sn: {}, sid: {}", iter->first, sid);
            return;
        }
        //send 响应头
        for(auto item : (*backendRespTpp)->backendRespHeaders) {
            evhtp_headers_add_header(iter->second->headers_out, evhtp_header_new(item.first.c_str(), item.second.c_str(), 1, 1));
        }
        //发送 响应体
        if((*backendRespTpp)->backendRespBodyLen > 0) {
            evbuffer_add(iter->second->buffer_out, (*backendRespTpp)->backendRespBody.c_str(), strlen((*backendRespTpp)->backendRespBody.c_str()));
        }
        evhtp_send_reply(iter->second, (*backendRespTpp)->backendRespStatus);
        if(that->baseInfo->serviceName.find("horizon") != std::string::npos) {
            evhtp_request_resume(iter->second);
        }
        logger->debug("resp to front end, sid: {}", sid);
    } else {
        logger->debug("req may already timeout, sid: {}", sid);
    }
    that->unlockInPj();
    //清理返回数据
    delete *backendRespTpp;
}

void MimicJudge::initTreadResource(evhtp_t *htp, evthr_t *thr, void *arg)
{
    MimicJudge *that = (MimicJudge*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    logger->debug("Thread resource init start: {}", 
    that->baseInfo->serviceName.c_str());

    //初始化judgectl
    judgeCtl_t *judgectl = new judgeCtl_t;
    judgectl->judge = that;
    pipe(judgectl->ctlFd);
    struct event* read_ev = event_new(evthr_get_base(thr), judgectl->ctlFd[0], EV_READ|EV_PERSIST, MimicJudge::replyToFront, judgectl);
    event_add(read_ev, NULL);
    that->lockInPj();
    that->jctlList.emplace_back(judgectl);
    that->unlockInPj();

    //安装
    evthr_set_aux(thr, judgectl);
    logger->debug("judgectl init success");
}

int MimicJudge::fillUpHeadesMap(evhtp_kv_t * kvobj, void * arg) {
    map<string, string> *headersMap = (map<string, string> *)arg;
    // (*headersMap)[kvobj->key] = kvobj->val;
    headersMap->emplace(kvobj->key, kvobj->val);
    return 0;
}

void MimicJudge::respToFrontNoJudgeError(evhtp_request_t *req) {
    if(this->baseInfo->serviceName.find("controller") != std::string::npos) {
        logger->debug("cloud controller dont to resp front");
        return;
    }
    logger->debug("headers error no judge error resp");
    evhtp_headers_add_header(req->headers_out,evhtp_header_new("Content-Type", "application/json", 1, 1));
    const char * time_out_msg = "{\"request_msg\":\"Gateway Time Out\"}"; 
    evbuffer_add(req->buffer_out, time_out_msg, strlen(time_out_msg));
    evhtp_send_reply(req,EVHTP_RES_GWTIMEOUT);
}

bool MimicJudge::checkReqHeaders(evhtp_request_t *req, map<string, string>& reqheaders) {
    if(reqheaders.find("backend") == reqheaders.end()) {
        logger->error("no backend find in headers");
        respToFrontNoJudgeError(req);
        return false;
    }   

    if(reqheaders.find("Server-Array") == reqheaders.end()) {
        logger->error("no server array find in headers");
        respToFrontNoJudgeError(req);
        return false;
    }

    if(reqheaders.find("msgid") == reqheaders.end()) {
        logger->error("no msgid find in headers");
        respToFrontNoJudgeError(req);
        return false;
    }

    if(reqheaders.find("Req-Info") == reqheaders.end()) {
        logger->error("no Req-Info find in headers");
        respToFrontNoJudgeError(req);
        return false;
    }

    if(reqheaders.find("Mimic-Host") == reqheaders.end()) {
        logger->error("no Mimic-Host find in headers");
        respToFrontNoJudgeError(req);
        return false;
    }

    return true;
}

evhtp_res MimicJudge::reqFinishCb(evhtp_request_t *req, void *arg) {
    MimicJudge *that = (MimicJudge*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    logger->debug("req finish cb");

    judgeCtl *judgectl = (judgeCtl_t *)evthr_get_aux(req->conn->thread);

    //云控不返回数据，清除所有list记录
    if(that->baseInfo->serviceName.find("controller") != std::string::npos) {
        logger->debug("controller clear all reqlist, reqlist size: {}", judgectl->frontReqList.size());
        that->lockInPj();
        judgectl->frontReqList.clear();
        that->unlockInPj();
        return 500;
    }

    //获取sid
    string sidstr;
    const char *sid= evhtp_header_find(req->headers_out,(const char *)"sid");
    if(sid == NULL) {
        sidstr = "nosid";
        logger->debug("sid not find, my timeout or err resped not in jthread, sid: {}", sidstr);
    } else sidstr = sid;
    //获取sn
    const char *sn= evhtp_header_find(req->headers_out,(const char *)"mimic-sn");
    if(sn == NULL) {
        logger->debug("mimic-sn not find, my timeout or err resped not in jthread, sid: {}", sidstr);
        return 500;
    }
    //通过sn获取req
    that->lockInPj();
    map<int, evhtp_request_t *>::iterator iter = judgectl->frontReqList.find(atoi(sn));
    if(iter == judgectl->frontReqList.end()) {
        logger->debug("connection is already deleted, sid: {}", sidstr);
    } else {
        judgectl->frontReqList.erase(iter);
        logger->debug("delete connection ok: sid, {}", sidstr);
    }
    that->unlockInPj();
    logger->debug("req list size: {}", judgectl->frontReqList.size());
    //redelete
    //shutdown(req->conn->sock,SHUT_RDWR);
    //close(req->conn->sock);
}

bool MimicJudge::checkMimicHostAndServerArray(Json::Value& value, string mimicHost) {
    if(value.type() != Json::arrayValue) {
        logger->debug("server array is not array");
        return false;
    }

    bool isFinded = false;
    for (int i = 0; i < value.size(); i++) {
        if(value[i].type() != Json::objectValue) {
            logger->debug("server array type error");
            return false;
        }
        string addr = value[i]["ip"].asString() + ":" + value[i]["port"].asString();
        if(addr == mimicHost) {
            isFinded = true;
            break;
        }
    }
    return isFinded;
}

void MimicJudge::allReqCallback(evhtp_request_t *req, void *arg)
{
    MimicJudge *that = (MimicJudge*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    logger->debug("Http msg come cb: {}", that->baseInfo->serviceName.c_str());

    //设置请求结束回调
    evhtp_request_set_hook(req,evhtp_hook_on_request_fini,(evhtp_hook)MimicJudge::reqFinishCb,that);

    //获取请求src port ip信息
    struct sockaddr_in* client = (sockaddr_in*)req->conn->saddr;
    string clientIp = inet_ntoa(client->sin_addr);
    uint16_t clientPort = client->sin_port;
    logger->debug("source ip: {}, source port: {}", clientIp, clientPort);

    //请求头列表及打钱日志
    map<string, string> reqheaders;
    evhtp_kvs_for_each(req->headers_in, MimicJudge::fillUpHeadesMap, &reqheaders);
    string headersraw = "\n";
    for(auto item : reqheaders) {
        headersraw += item.first + ": " + item.second + "\n";
    }
    logger->debug("reqheaders: {}", headersraw);

    //判断是否有sid无sid返回失败
    if(reqheaders.find("sid") == reqheaders.end()) {
        logger->error("no sid find in headers");
        that->respToFrontNoJudgeError(req);
        return;
    }

    //check headers;
    if(!that->checkReqHeaders(req, reqheaders)) return;
    Json::Reader reader;
    Json::Value value;
    try {
        reader.parse(reqheaders["Server-Array"], value);
    } catch (...) {
        logger->error("parse server array error");
        that->respToFrontNoJudgeError(req);
        return;
    }

    if (!that->checkMimicHostAndServerArray(value, reqheaders["Mimic-Host"])) {
        that->respToFrontNoJudgeError(req);
        return;
    }

    logger->debug("to init judge data: {}", reqheaders["sid"]);
    that->initJudgeData(req, reqheaders, value.size());
}


void MimicJudge::initJudgeData(evhtp_request_t *req, map<string, string> reqheaders, int execNum) {
    //sid
    string sid = reqheaders["sid"];
    //获取judgectl
    judgeCtl_t *judgectl = (judgeCtl_t *)evthr_get_aux(req->conn->thread);
    //初始化judgetask
    RightJudgeDada_t *judgeData = new RightJudgeDada_t;

    //赋值返回路径
    judgeData->sid = sid;
    judgeData->replyfd = judgectl->ctlFd[1];
    judgeData->execNum = execNum;

    //juctl中req编号
    lockInPj();
    if (sn == INT_MAX) sn = 0;
    judgeData->sn = sn;
    judgectl->frontReqList.emplace(sn, req);
    sn++;
    unlockInPj();
    logger->debug("mimic sn in judge data: {}, sid: {}", judgeData->sn, sid);

    //请求头
    judgeData->reqHeaders = std::move(reqheaders);
    judgeData->reqHeaders["mimic-sn"] = std::to_string(judgeData->sn);
    logger->debug("judge req reqheaders size: {}, sid: {}", judgeData->reqHeaders.size(), sid);

    //url
    logger->debug("url all: {}", req->uri->path->full);
    judgeData->urlall = req->uri->path->full == NULL ? "/" : req->uri->path->full;
    judgeData->urlfull = req->uri->path->full == NULL ? "/" : req->uri->path->full;
    judgeData->urlraw = req->uri->path->full == NULL ? "/" : req->uri->path->full;

    //请求体生成
    char *body = NULL;
    size_t bodyLen = 0;
    bodyLen = evbuffer_get_length(req->buffer_in);
    if(bodyLen > 0 && req->method != htp_method_HEAD) {
        body = (char *)malloc(bodyLen + 1);
        memset(body, 0x00,bodyLen + 1);
        evbuffer_copyout(req->buffer_in, body, bodyLen);
        if(strlen(body) != bodyLen) {
            logger->warn("judge req body len not same, sid: {}", sid);
        }
    }
    judgeData->body = body == NULL ? "" : body;
    if(body != NULL) free(body);
    logger->debug("judge req body, size: {}, sid: {}", judgeData->body.size(),sid);

    //bodylen
    judgeData->bodyLen = bodyLen;

    //content-type
    if(judgeData->reqHeaders.find("Content-Type") != judgeData->reqHeaders.end()) {
        judgeData->contentType = judgeData->reqHeaders["Content-Type"];
    } else if(judgeData->reqHeaders.find("content-type") != judgeData->reqHeaders.end()) {
        judgeData->contentType = judgeData->reqHeaders["content-type"];
    } else {
        judgeData->contentType = "";
    }

    //裁决辅助字段
    judgeData->group = -1;
    judgeData->key = sid + "-" + judgeData->reqHeaders["Mimic-Host"];
    judgeData->execAddr = judgeData->reqHeaders["Mimic-Host"];
    judgeData->method = evhtp_request_get_method(req);
    //云管需要回复
    if(baseInfo->serviceName.find("horizon") != std::string::npos) {
        evhtp_request_pause(req);
    }
    //获取jthread
    int threadIndex = 0;
    for(int i = 0; i < strlen(sid.c_str()); i++) {
        if(std::to_string(sid[i]) == "-" || std::to_string(sid[i]) == "+") break;
        threadIndex += atoi(std::to_string(sid[i]).c_str());
    }
    threadIndex = threadIndex % baseInfo->workThreadNum;
    MimicJthread *jthread = jthreadList[threadIndex];

    //发送数据向jthread
    RightJudgeDada_t **tp = &judgeData;
    lockInPj();
    int rc = write(jthread->threadFd[1], tp, sizeof(RightJudgeDada_t *));
    unlockInPj();
    if(rc > 0) {
        logger->debug("send to jthread ok, sid: {}", sid);
    } else {
        lockInPj();
        if(judgectl->frontReqList.find(judgeData->sn) != judgectl->frontReqList.end())
            judgectl->frontReqList.erase(judgectl->frontReqList.find(judgeData->sn));
        unlockInPj();
        logger->error("send to jthread fail, sid:{}", sid);
        delete judgeData;
        respToFrontNoJudgeError(req);
    }
}

void MimicJudge::registCallbacks()
{
    this->urlCallbackFun = MimicJudge::allReqCallback;
    this->threadInitCb = MimicJudge::initTreadResource;
    this->sigIntCb = MimicJudge::sigIntCbFun;
}

string MimicJudge::convertFromCincerOrNeutron(string saddr) {
    bool iscinder = false;
    bool isneutron = false;
    if(saddr.find("9696") != std::string::npos) isneutron = true;
    if(saddr.find("8776") != std::string::npos)  iscinder = true;

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
