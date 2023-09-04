#ifndef MIMIC_PROXY
#define MIMIC_PROXY

#include "mimic_struct.hpp"
#include "mimic_service.hpp"
#include "mimic_util.hpp"
#include "mimic_suber.hpp"
#include "mimic_task.hpp"

class MimicProxy:public MimicProxyJudge
{
public:
    bool isKeystone;
    bool iscinder;
    bool isneutron;
    bool ishorizon;

    vector<MimicCommon*> commons;
    vector<MimicSuber*> subers;
    map<string, ProxyTask*> proxyTaskList;
    map<string, int> judgeErrNum;

    MimicProxy(GlobalInfo_t *globalConf, vector<string> *ignoreKeys, ServiceBaseInfo_t *baseInfo);
    virtual ~MimicProxy();
    static void sigIntCbFun(int sig, short why, void *arg);
    static void initTreadResource(evhtp_t *htp, evthr_t *thr, void *arg);
    static void allReqCallback(evhtp_request_t *req, void *arg);
    void registCallbacks();
    //生成sid并创建task
    void makeSidTask(evhtp_request_t *req);
    //回复前端
    void respToFront(evhtp_request_t *req, ProxyTask* proxyTask);
    //错识误回复前端
    void respToFrontNoProxyError(evhtp_request_t *req);
    //清理task
    void clearTask(string sid);

    string convertFromCincerOrNeutron(string saddr);

    void doManualOpt(evhtp_request_t *req, string manual);
    void deal_monitor_exec_manual(evhtp_request_t *req);
    void deal_monitor_config_policy(evhtp_request_t *req);
    void deal_monitor_config_scweight(evhtp_request_t *req);
    void deal_monitor_exec_get_horizon_policy(evhtp_request_t *req);
    void deal_monitor_exec_get_nova_policy(evhtp_request_t *req);
    void deal_monitor_exec_get_keystone_policy(evhtp_request_t *req);
    void deal_config_mimic_switch(evhtp_request_t *req);
    void deal_get_mimic_switch(evhtp_request_t *req);
};

#endif
