#include <unistd.h>
#include <sys/syscall.h>
#include "mimic_judge.hpp"

#include "mimic_jthread.hpp"

MimicJthread::MimicJthread(MimicCommon *common, MimicService *service):
common(common), service(service) {
    this->logger = service->logger;
    common = nullptr;
    suber = nullptr;
    this->mutexInjthread = PTHREAD_MUTEX_INITIALIZER;
}

MimicJthread::~MimicJthread() {
    if (common != nullptr) delete common;
    if (suber != nullptr) delete suber;
}

void *MimicJthread::jthreadStartCb(void *arg) {
    MimicJthread *that = (MimicJthread*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    //阻塞
    logger->debug("jthread loop");
    event_base_dispatch(that->evbase);
    logger->debug("jthread exit loop");
}

void MimicJthread::jthreadStop() {
    logger->debug("stop jthread");
    //退出阻塞
    event_base_loopexit(evbase, NULL);
}

void MimicJthread::clearTask(string sid) {
    pthread_mutex_lock(&(suber->mutexInSuber));;
    map<string, JudgeTask*>::iterator iter = taskList.find(sid);
    if(iter == taskList.end()) {
        logger->debug("this sid cant find task in judge: {}", sid);
    } else {
        logger->debug("delete judge task start: {}", sid);
        event_del(iter->second->time_out_ev);
        event_free(iter->second->time_out_ev);
        delete iter->second;
        taskList.erase(iter);
        logger->debug("delete judge task end: {}", sid);
    }
    pthread_mutex_unlock(&(suber->mutexInSuber));;
}

void MimicJthread::initjThread(int index) {
    logger->debug("init judge thread pipe");
    //绑定pipe
    pipe(threadFd);
    evbase = event_base_new();
    struct event* readEv = event_new(evbase, threadFd[0], EV_READ|EV_PERSIST, MimicJthread::jthreadReadCb, this);
    event_add(readEv, NULL);

    //订阅初始化
    logger->debug("init judge thread suber");
    suber = new MimicSuber(common, this->service, evbase);
    suber->subscribe();
    string inittid = std::to_string(syscall(SYS_gettid));
    suber->threadID = inittid;

    //添加jthreadlist
    MimicJudge *judgeService = dynamic_cast<MimicJudge *>(this->service);
    judgeService->jthreadList.emplace(index, this);

    //创建裁决线程
    pthread_create(&tid, NULL, MimicJthread::jthreadStartCb, (void *)this);
}

void MimicJthread::initTaskFromFistReq(JudgeTask *task, map<string, string>& reqheaders) {
    Json::Reader reader;
    Json::Value value;
    //解析server-array
    reader.parse(reqheaders["Server-Array"], value);

    //初始化相关内容
    pthread_mutex_lock(&(suber->mutexInSuber));
    for(int i = 0; i < value.size(); i++) {
        task->failcountList.emplace(value[i]["ip"].asString() + ":" + value[i]["port"].asString(), 
        suber->allExecList[value[i]["ip"].asString() + ":" + value[i]["port"].asString()].failCount);

        task->uptimelist.emplace(value[i]["ip"].asString() + ":" + value[i]["port"].asString(), 
        suber->allExecList[value[i]["ip"].asString() + ":" + value[i]["port"].asString()].activeTime);

        task->serverarray.emplace_back(value[i]["ip"].asString() + ":" + value[i]["port"].asString());
    }
    task->thrId = suber->threadID;
    task->judgeRule = suber->judgeRule;
    pthread_mutex_unlock(&(suber->mutexInSuber));
    task->suber = suber;
}

void MimicJthread::sendToFrontNoJudgeError(RightJudgeDada_t *judgeData) {
    logger->debug("headers error no judge error resp, sid: {}", judgeData->sid);
    //创建返回值结构体
    backendResp_t *errResp = new backendResp_t;
    errResp->backendRespHeaders["sid"] = judgeData->sid;
    errResp->backendRespHeaders["Content-Type"] = "application/json";
    errResp->backendRespHeaders["mimic-sn"] = std::to_string(judgeData->sn);
    errResp->backendRespStatus = EVHTP_RES_GWTIMEOUT;
    string time_out_msg = "{\"request_msg\":\"Gateway Time Out\"}"; 
    errResp->backendRespBody = time_out_msg;
    errResp->backendRespBodyLen = strlen(time_out_msg.c_str());
    //发送工作线程
    backendResp_t **tp = &errResp;
    pthread_mutex_lock(&mutexInjthread);;
    int rc = write(judgeData->replyfd, tp, sizeof(backendResp_t *));
    pthread_mutex_unlock(&mutexInjthread);;
    if(rc > 0) {
        logger->debug("send to http thread ok, sid: {}", judgeData->sid);
    } else {
        logger->error("send to http thread fail, may no resp, sid: {}", judgeData->sid);
    }
    //清理数据
    delete judgeData;
}

void MimicJthread::doJudge(RightJudgeDada_t *judgeData) {
    string sid = judgeData->sid;
    //获取task
    JudgeTask *task = NULL;
    pthread_mutex_lock(&mutexInjthread);
    if(taskList.find(sid) == taskList.end()) {
        logger->debug("create task, {}", sid);
        task = new JudgeTask(sid, evbase, this, judgeData->execNum);
        //初始化相关变量
        initTaskFromFistReq(task, judgeData->reqHeaders);
        //加入taskList
        taskList.emplace(sid, task);
    } else {
        logger->debug("exsit task, {}", sid);
        task = taskList[sid];
    }
    pthread_mutex_unlock(&mutexInjthread);

    //检查相关内容是否合法
    if(task == NULL) {
        logger->debug("cant find task, sid: {}", sid);
        sendToFrontNoJudgeError(judgeData);
        return;
    }
    //header头不合法
    if(find(task->serverarray.begin(), task->serverarray.end(), judgeData->reqHeaders["Mimic-Host"]) == task->serverarray.end()) {
        logger->debug("cant find Mimic-Host in task server array, sid: {}", sid);
        sendToFrontNoJudgeError(judgeData);
        return;
    }
    //sid数据超过应到数量
    if(task->judgeDataList.size() == task->serverarray.size()) {
        logger->error("already reach judge data max size, sid num > {}, datalist size: {}, sid: {}", judgeData->execNum, task->judgeDataList.size(), sid);
        sendToFrontNoJudgeError(judgeData);
        return;
    }

    //加入List
    pthread_mutex_lock(&(task->mutexRightJudge));
    if(task->respIsOk.find(judgeData->reqHeaders["Mimic-Host"]) == task->respIsOk.end()) {
        task->respIsOk.emplace(judgeData->reqHeaders["Mimic-Host"], "no");
    } else {
        logger->debug("exist req in task, sid: {}", sid);
        sendToFrontNoJudgeError(judgeData);
        pthread_mutex_unlock(&(task->mutexRightJudge));
        return;
    }
    pthread_mutex_unlock(&(task->mutexRightJudge));

    //修正datakey
    judgeData->key = judgeData->key + "-" + std::to_string(task->judgeDataList.size() + 1);
    //进入裁决过程
    task->processJudgeData(judgeData, judgeData->key);
}

void MimicJthread::jthreadReadCb(int sock, short event, void* arg) {
    MimicJthread *that = (MimicJthread*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    logger->debug("in jthread recv cb");

    //获取请求待裁决数据结构体
    char *recvbuf[10] = {0};
    int rc = read(sock, recvbuf, sizeof(RightJudgeDada_t*));
    RightJudgeDada_t **rightJudgeDataRecv = (RightJudgeDada_t **)recvbuf;

    //打印日志
    char strlog[1024] = {0};
    sprintf(strlog, "judge task read cb[%p][%p]", rightJudgeDataRecv, *rightJudgeDataRecv);
    logger->debug(strlog);

    //判断定阅内容是否为空
    if (that->suber->allExecList.empty() || that->suber->activeExecList.empty() || 
        that->suber->judgeErrorCount.empty() || that->suber->judgeRule.empty()) {
        logger->error("exec info and judge policy sync fail, sid: {}", (*rightJudgeDataRecv)->sid);
        that->sendToFrontNoJudgeError(*rightJudgeDataRecv);
        return;
    }

    //开始裁决
    that->doJudge(*rightJudgeDataRecv);
}