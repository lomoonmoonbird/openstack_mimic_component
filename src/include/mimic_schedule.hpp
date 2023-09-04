#ifndef MIMIC_SCHEDULE
#define MIMIC_SCHEDULE

#include <pthread.h>
#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/util.h>
#include <pthread.h>

#include "json/json.h"
#include "mimic_struct.hpp"
#include "mimic_service.hpp"
#include "mimic_util.hpp"
#include "mimic_suber.hpp"
#include "mimic_timer.hpp"


class MimicSchedule:public MimicService
{
public:
    pthread_mutex_t mutexExecs;
    bool isFirstStart;
    bool isHorizon;
    bool isController;
    bool isKeystone;
    string testUrl;
    string execStatusInfoTb;
    string execLogInfoTb;

    // bool isCleaning;

    MimicTimer *autoReportTimer;
    MimicTimer *autoScheduleTimer;
    MimicCommon *common;
    MimicSuber* suber;

    int manualDownNum;
    map<int, string> portToExecOs;
    // map<string, int> prevCleanStatus;
    map<string, uint64_t> preCleanTimes;
    vector<struct bufferevent *> subscribeBevs;
    map<string, struct evhttp_connection *> addrConn;

    MimicSchedule(GlobalInfo_t *globalConf, ServiceBaseInfo_t *baseInfo);
    virtual ~MimicSchedule();
    void execStatusReport();
    void execAtuoSchedule();

    bool initMysqlAndPub();
    void syncExecLog(string addr, string relatedAddr, string action, string status, string msg);
    void syncAndPub(struct bufferevent *bev);
    void updateExecStatusDb(string ip, int port, int status);
    void doUpdown(string addr, string msg);
    
    void initActiveExec();
    string policy_all_random(string downAddr="");
    string policy_weight(string downAddr="");
    string policy_host_random(string downAddr="");

    void doQueryUpExecs(struct bufferevent *bev);
    void doQueryExecStats(struct bufferevent *bev);
    void doQueryUpTimes(struct bufferevent *bev);
    void doHungDown(struct bufferevent *bev, Json::Value& value);
    void doHungUp(struct bufferevent *bev, Json::Value& value);

    void doManualUpOpt(struct bufferevent *bev, Json::Value& value);
    void doManualDownOpt(struct bufferevent *bev, Json::Value& value);
    void doManualCleanOpt(struct bufferevent *bev, Json::Value& value);
    void doReportError(struct bufferevent *bev, Json::Value& value);
    void doSubscribe(struct bufferevent *bev);
    void doUpdateConfig(struct bufferevent *bev, Json::Value& value);

    void cleanExec(string addr);
    
    static void sigIntCbFun(int sig, short why, void *arg);
    static void execAutoReportCb(struct evhttp_request *req, void *arg);
    void registCallbacks();
    bool initSubber();
    int initTimer();
    virtual void serviceSpawn();

    static void *cleanThread(void *arg);

    static void listenerCb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *addr, int len, void *ptr);
    static void eventCb(struct bufferevent *bev, short events, void *arg);
    static void readCb(struct bufferevent *bev, void *arg);
    static void writeCb(struct bufferevent *bev, void *arg);
    void respMsg(struct bufferevent *bev, string status, Json::Value& value);
    void doUpdateScweight(struct bufferevent *bev, Json::Value& value);
};

#endif
