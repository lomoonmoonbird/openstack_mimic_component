#ifndef MIMIC_SUBER
#define MIMIC_SUBER

#include "mimic_util.hpp"
#include "mimic_struct.hpp"
#include "mimic_service.hpp"

#include "json/json.h"


class MimicSuber
{
private:
public:
    pthread_mutex_t mutexInSuber;
    bool isHorizon;
    bool isController;
    bool isKeystone;
    bool isProxy;
    bool isNova;
    bool isCinder;
    bool isNeutron;

    bool shouldBeConvert;
    bool isInit;

    bool isSchedule;
    string threadID;

    struct event_base* base;
    MimicService *service;
    std::shared_ptr<spdlog::logger> logger;
    MimicCommon *common;
    string sername;

    string judgeRule;
    string judgeErrorCount;
    string scheduleRule;
    string scheduleInterval;
    //仅供前端查询使用，不进行发布
    // string schedulePolicySelect;
    string mimicSwitch;

    map<string, executor_t> activeExecList;
    map<string, executor_t> allExecList;

    MimicSuber(MimicCommon *common, MimicService *service, struct event_base* base);
    ~MimicSuber();

    static void readCb(struct bufferevent *bev, void *arg);
    static void writeCb(struct bufferevent *bev, void *arg);
    static void eventCb(struct bufferevent *bev, short events, void *arg);

    int subscribe();
    void initBoolFlag();
    void syncPolicyFromMysql();
    void syncExecFromMysql();
    int fillExecMap(Json::Value& value);
    int setPolicys(Json::Value& value);
    int convertToCincerOrNeutron(int sport);
};

#endif
