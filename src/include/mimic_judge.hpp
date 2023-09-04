#ifndef MIMIC_JUDGE
#define MIMIC_JUDGE

#include "mimic_struct.hpp"
#include "mimic_service.hpp"
#include "mimic_util.hpp"
#include "mimic_suber.hpp"
#include "mimic_task.hpp"
#include "mimic_jthread.hpp"

class MimicJudge:public MimicProxyJudge
{
public:
    int sn;
    map<string, int> judgeErrNum;
    map<int, MimicJthread*> jthreadList;
    vector<judgeCtl_t *> jctlList;

    MimicJudge(GlobalInfo_t *globalConf, vector<string> *ignoreKeys, ServiceBaseInfo_t *baseInfo);
    virtual ~MimicJudge();
    static void sigIntCbFun(int sig, short why, void *arg);
    static void initTreadResource(evhtp_t * htp, evthr_t * thr, void * arg);
    static void allReqCallback(evhtp_request_t *req, void *arg);
    void registCallbacks();

    void respToFrontNoJudgeError(evhtp_request_t *req);
    static int fillUpHeadesMap(evhtp_kv_t * kvobj, void * arg);
    void initJudgeData(evhtp_request_t *req, map<string, string> reqheaders, int execNum);
    bool checkReqHeaders(evhtp_request_t *req, map<string, string>& reqheaders);
    static evhtp_res reqFinishCb(evhtp_request_t * req, void * arg);

    string convertFromCincerOrNeutron(string saddr);

    static void replyToFront(int fd, short event, void *arg);
    bool checkMimicHostAndServerArray(Json::Value& value, string mimicHost);
};

#endif
