#ifndef MIMIC_TASK
#define MIMIC_TASK

#include <string>
#include <vector>
#include <tuple>
#include "mimic_struct.hpp"
#include "mimic_service.hpp"
#include "spdlog/spdlog.h"
#include "json/json.h"
#include "mimic_suber.hpp"
#include <pthread.h>

class ProxyTask
{
public:
    struct timeval rTimeout;
    struct timeval wTimeout;
    string sid;
    MimicService* service;
    std::shared_ptr<spdlog::logger> logger;
    vector<string> *ignoreKeys;
    string thrId;
    string judgeRule;
    string judgeType;

    evbase_t *evbase;
    string retGroupKey;
    MimicSuber *suber;
    evhtp_request_t *frontreq;
    string urlfull;
    string params;
    htp_method httpmethod;
    pthread_mutex_t mutexLeftJudge;
    vector<string> addrList;
    map<string, string> reqHeaders;
    map<string, LeftJudgeDada_t*> judgeDataList;
    LeftJudgeDada_t *judgeResult;
    map<LeftJudgeDada_t *, vector<string>> judgeGroup;
    map<string, vector<judgeDiffRecord_t>> cmpRetRecord;
    map<string, judgeDiffRecord_t> analyseErrorDetailMap;

    map<string, int> failcountList;
    map<string, uint64_t> uptimelist;

    ProxyTask(string sid, MimicService* service, evhtp_request_t *frontreq);
    ~ProxyTask();

    void startTask();

    //new
    static int fillUpHeadesMap(evhtp_kv_t * kvobj, void * arg);
    int initHeaders(vector<string>& addrList, map<string, string>& reqHeaders);

    void addErrorJudgeData(int countp, string execAddr);
    //todo
    static void proxyReqCb(evhtp_request_t * req, void * arg);
    static evhtp_res proxyReqErrCb(evhtp_request_t * req, evhtp_error_flags errtype, void * arg);
    static evhtp_res proxyReqFinishCb(evhtp_request_t * req, void * arg);

    int judgeData(string key);
    int cmpJudgeData(LeftJudgeDada_t *gorupData, vector<string>& groupkeys, LeftJudgeDada_t *comData);
    void analyseJudgeResult();
    LeftJudgeDada_t *timeOrWeightJudge();
    std::tuple<int, string, string, Json::Value, Json::Value> comJsonBody(string groupBody, string comBody);
    int comTxtBody(string groupBody, string comBody);
    std::tuple<int, string, string, string> comJsonStr(Json::Value value1, Json::Value value2);
    string filterBody(string body);
    void flatJson(const Json::Value& root, const string& key,const string& jsonType, std::vector<string>& flatted, 
    vector<string> &ignoreAccurateKeys, vector<string> &ignoreFuzzyKeys, const string &keySeparator="->", const string &valueSeparator=":", const string &typeSeparator="@", 
    const string &typeGroupSeparator="|");
    void recordCmpErrorRet(LeftJudgeDada_t *gorupData, LeftJudgeDada_t *comData, string error_reason, string error_detail);
    //TODO
    void recordJudgeLog(string jsonstr);
    void sendToSchedule();
    void clearNewData();
};

class JudgeTask
{
public:
    struct timeval rTimeout;
    struct timeval wTimeout;
    bool backendRespCome;
    bool isClean;
    bool tcpsendend;
    bool isRespStared;

    void* jthread;
    std::shared_ptr<spdlog::logger> logger;
    int execNum;
    MimicSuber *suber;
    vector<string> serverarray;

    int judgeNum;
    int judgeNumCtr;

    RightJudgeDada_t *firstReqData;

    struct timeval ev_tv;
    struct event *time_out_ev;

    map<string, string> backendRespHeaders;
    int backendRespStatus;
    string backendRespBody;
    uint64_t backendRespBodyLen;

    vector<string> *ignoreKeys;
    string thrId;
    string judgeRule;
    string judgeType;
    string urlfull;

    string sid;
    evbase_t *evbase;
    pthread_mutex_t mutexRightJudge;

    RightJudgeDada_t *judgeResult;
    map<string, RightJudgeDada_t *> judgeDataList;
    map<RightJudgeDada_t *, vector<string>> judgeGroup;
    map<string, vector<judgeDiffRecord_t>> cmpRetRecord;
    map<string, judgeDiffRecord_t> analyseErrorDetailMap;
    string retGroupKey;

    map<string, int> failcountList;
    map<string, uint64_t> uptimelist;
    map<string, string> respIsOk;

    JudgeTask(string sid, evbase_t *evbase, void* jthread, int execNum);
    ~JudgeTask();

    void processJudgeData(RightJudgeDada_t *judgedata, string key);

    int judgeData(string key);
    int cmpJudgeData(RightJudgeDada_t *gorupData, vector<string>& groupkeys, RightJudgeDada_t *comData);
    void analyseJudgeResult();
    RightJudgeDada_t *timeOrWeightJudge();
    std::tuple<int, string, string, Json::Value, Json::Value> comJsonBody(string groupBody, string comBody);
    int comTxtBody(string groupBody, string comBody);
    std::tuple<int, string, string, string> comJsonStr(Json::Value value1, Json::Value value2);
    string filterBody(string body);
    void flatJson(const Json::Value& root, const string& key,const string& jsonType, std::vector<string>& flatted, 
    vector<string> &ignoreAccurateKeys, vector<string> &ignoreFuzzyKeys, const string &keySeparator="->", const string &valueSeparator=":", const string &typeSeparator="@", 
    const string &typeGroupSeparator="|");
    void recordCmpErrorRet(RightJudgeDada_t *gorupData, RightJudgeDada_t *comData, string error_reason, string error_detail);

    static void judgeTimeoutCb(evutil_socket_t fd, short event, void *arg);
    void sendToBackend(string addr);
    void gotoErrBackendResp();
    void responseToFrondReq(bool isFromProcess);

    static void backendRespCb(evhtp_request_t * req, void * arg);
    static evhtp_res backendRespErrCb(evhtp_request_t * req, evhtp_error_flags errtype, void * arg);
    static evhtp_res backendRespFinishCb(evhtp_request_t * req, void * arg);
    static int fillUpHeadesMap(evhtp_kv_t * kvobj, void * arg);

    void recordJudgeLog(string jsonstr);
    void sendToSchedule();
    void clearNewData();
};

#endif
