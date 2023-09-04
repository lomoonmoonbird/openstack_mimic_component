#ifndef MIMIC_SERVICE
#define MIMIC_SERVICE

#include <map>
#include <vector>
#include <deque>
#include <pthread.h>

#include "spdlog/spdlog.h"
#include "mimic_struct.hpp"


class MimicService
{
public:
    struct timeval rTimeout;
    struct timeval wTimeout;
    struct event *evSigint;
    evbase_t *evbase;
    evhtp_t *htp;
    //存储redis mysql log配置
    GlobalInfo_t *globalConf;
    ServiceBaseInfo_t * baseInfo;
    UrlCallbackFun urlCallbackFun;
    //信号处理
    SigIntCb sigIntCb;
    ThreadInitCb threadInitCb;
    std::shared_ptr<spdlog::logger> logger;
    vector<execBaseInfo_t> execInfo;

    virtual ~MimicService();
    void initBaseInfo(GlobalInfo_t *globalConf, ServiceBaseInfo_t *baseInfo);
    //日志初始化
    void initLogger();
    //注册回调函数子类实现
    virtual void registCallbacks() = 0;
    //调度实现不同
    virtual void serviceSpawn();
    bool initJudgeThread();
};

class MimicProxyJudge:public MimicService
{
private:
    string judgeRule;
    pthread_mutex_t mutexInPj;
public:
    deque<bool> initThreadRes;
    vector<string> *ignoreKeys;
    virtual ~MimicProxyJudge();
    void initProxyJudgeInfo(GlobalInfo_t *globalConf, vector<string> *ignoreKeys, ServiceBaseInfo_t *baseInfo);
    static void subscribeJudgeRule(void *arg);
    void lockInPj();
    void unlockInPj();
    void recordTcpClientResp(string resp, void *arg);
};

#endif
