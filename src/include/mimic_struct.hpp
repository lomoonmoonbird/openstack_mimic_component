#ifndef MIMIC_STRUCT
#define MIMIC_STRUCT

#include <evhtp.h>
#include <string>
#include <vector>
#include <map>
#include <pthread.h>
#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/util.h>
#include <arpa/inet.h>

#include "/usr/include/mysql/mysql.h"


#define LOG_PATH "/var/log/mimic/"

using namespace std;

typedef void (*UrlCallbackFun) (evhtp_request_t *req, void *arg);
typedef void (*ThreadInitCb) (evhtp_t * htp, evthr_t *thr, void *arg);
typedef void (*SigIntCb) (int sig, short why, void *arg);
// typedef void (*TimerCb) (evutil_socket_t fd, short event, void *arg);

struct GlobalInfo
{
    int execNum;
    string mysqlHost;
    int mysqlPort;
    string mysqlUser;
    string mysqlPassword;
    string mysqlDb;

    string schedule_horizon_ip;
    int schedule_horizon_port;

    string schedule_controller_ip;
    int schedule_controller_port;

    string schedule_keystone_ip;
    int schedule_keystone_port;
};

struct ServiceBaseInfo
{
    string serviceName;
    //服务ip
    string host;
    //服务端口
    int port;
    //服务线程数量
    int workThreadNum;
    //超时时间
    int timeout;

    int reportInterval;
};

struct MysqlExecRes
{
    bool isSelect;
    bool isSuccess;
    vector<map<string,string>> result;
};

struct executor
{
   int id;
   int status;
   int execZone;
   string systemNum;
   string arch;
   string note;
   string cpu;
   string ip;
   int port;
   string web;
   string os;
   int scweight;
   uint64_t activeTime;

   int failCount;
};

struct execJudgeSuccessFailCount
{
    int successCount;
    int failCount;
};

struct judgeDiffRecord
{
    string judgeDataKey;
    string error_reason;
    string error_detail;
};

enum services
{
    schedule_horizon = 1,
    schedule_controller,
    schedule_keystone,
    proxy_horizon,
    proxy_nova,
    proxy_cinder,
    proxy_neutron,
    proxy_keystone,
    judge_horizon,
    judge_controller
};

struct execBaseInfo
{
    string ip;
    int port;
    string os;
    string web;
    string arch;
};

struct addr
{
    string ip;
    int port;
};

struct LeftJudgeDada
{
    string sid;
    int respStatus;
    map<string, string> respHeaders;
    string contentType;
    string body;
    uint64_t bodyLen;
    string urlfull;
    htp_method method;
    string params;
    int group;
    string key;
    string execAddr;
};

struct RightJudgeDada
{
    string sid;
    int replyfd;
    int sn;
    int execNum;

    string urlfull;
    string urlraw;
    string urlall;
    htp_method method;
    map<string, string> reqHeaders;
    string contentType;
    string body;
    uint64_t bodyLen;

    int group;
    string key;
    string execAddr;
};

struct LeftTmpArgs
{
    string execAddr;
    int coutp;
    void * proxyTask;
};

struct backendResp
{
    map<string, string> backendRespHeaders;
    int backendRespStatus;
    string backendRespBody;
    uint64_t backendRespBodyLen;
};

struct judgeCtl
{
    int ctlFd[2];
    map<int, evhtp_request_t *> frontReqList;
    void *judge;
};

struct weightRange
{
    int start, end;
    //重载操作符 当executor结构体当map的key时候需要判断是否是同一个key
    bool operator< (const weightRange &e) const{
        return start < e.start;
    }
};

typedef struct execJudgeSuccessFailCount execJudgeSuccessFailCount_t;
typedef struct addr addr_t;
typedef struct execBaseInfo execBaseInfo_t;
typedef struct GlobalInfo GlobalInfo_t;
typedef struct ServiceBaseInfo ServiceBaseInfo_t;
typedef struct MysqlExecRes MysqlExecRes_t;
typedef struct executor executor_t;
typedef enum services services_t;
typedef struct LeftJudgeDada LeftJudgeDada_t;
typedef struct LeftTmpArgs LeftTmpArgs_t;
typedef struct judgeDiffRecord judgeDiffRecord_t;
typedef struct RightJudgeDada RightJudgeDada_t;
typedef struct backendResp backendResp_t;
typedef struct judgeCtl judgeCtl_t;
typedef struct weightRange weightRange_t;
// class MimicExecutor
// {
// private:
//     map<string, executorInfo_t> allExecutorList;
//     map<string, executorInfo_t> activeExecutorList;
//     pthread_mutex_t mutex; 
//     pthread_t executorSubscribe;
// public:
//     static subscribReisExecInfo();
//     void lock();
//     void unlock();
// }

#endif