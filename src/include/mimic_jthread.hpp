#ifndef MIMIC_JTHREAD
#define MIMIC_JTHREAD

#include <pthread.h>

#include "mimic_struct.hpp"
#include "mimic_util.hpp"
#include "mimic_suber.hpp"
#include "mimic_task.hpp"

class MimicJthread
{
public:
    pthread_mutex_t mutexInjthread;

    int threadFd[2];
    struct event_base *evbase; 
    pthread_t tid;

    map<string, JudgeTask*> taskList;

    MimicCommon *common;
    MimicService *service;
    MimicSuber* suber;
    std::shared_ptr<spdlog::logger> logger;

    MimicJthread(MimicCommon *common, MimicService *service);
    ~MimicJthread();

    void initjThread(int index);
    static void *jthreadStartCb(void *arg);
    static void jthreadReadCb(int sock, short event, void* arg);
    void jthreadStop();

    void doJudge(RightJudgeDada_t *judgeData);
    void initTaskFromFistReq(JudgeTask *task, map<string, string>& reqheaders);
    void clearTask(string sid);
    void sendToFrontNoJudgeError(RightJudgeDada_t *judgeData);
};

#endif
