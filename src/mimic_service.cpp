#include <iostream>
#include <csignal>
#include <cstdio>
#include <unistd.h>
#include <sys/prctl.h>
#include "evhtp.h"
#include "mimic_util.hpp"

#include "spdlog/spdlog.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "mimic_service.hpp"
#include "mimic_jthread.hpp"

MimicService::~MimicService(){
    if(baseInfo != nullptr) {
        delete baseInfo;
        baseInfo = nullptr;
    }
}

void MimicService::initBaseInfo(GlobalInfo_t *globalConf, ServiceBaseInfo_t *baseInfo)
{   
    this->baseInfo = baseInfo;
    this->globalConf = globalConf;

    memset(&rTimeout,0x00,sizeof(rTimeout));
    memset(&wTimeout,0x00,sizeof(wTimeout));
    this->rTimeout.tv_sec = baseInfo->timeout;
    this->wTimeout.tv_sec = baseInfo->timeout;
}

void MimicService::initLogger()
{
    char log_path[40] = {0};
    sprintf(log_path, "/var/log/mimic-cloud/%s.log", this->baseInfo->serviceName.c_str());

    char logger_name[40] = {0};
    sprintf(logger_name, "%s_logger", this->baseInfo->serviceName.c_str());

    // logger = spdlog::daily_logger_mt(logger_name, log_path, 2, 30);
    logger = spdlog::basic_logger_mt(logger_name, log_path);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%L%$] [%t] %v");
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::debug);
    // spdlog::flush_every(std::chrono::seconds(1));

    logger->debug("Logger init end, service name: {}", this->baseInfo->serviceName);
}

bool MimicService::initJudgeThread() 
{
    for(int i = 0; i < baseInfo->workThreadNum; i++) {
        MimicCommon *common = new MimicCommon(this->globalConf, logger);
        try{
            common->initMysql();
        } catch (const char* &e) {
            logger->error("mysql Init fail: {}", e);
            // return false;
        }
        // logger->debug("mysql Init success");

        MimicJthread * jthread = new MimicJthread(common, this);
        jthread->initjThread(i);
    }
    return true;
}

void MimicService::serviceSpawn() 
{
    prctl(PR_SET_NAME, baseInfo->serviceName.c_str(), NULL, NULL, NULL);
    //裁决线程分离开
    if(baseInfo->serviceName.find("judge") != std::string::npos) {
        if (!initJudgeThread())
            exit(1);
    }
    //evhtp线程
    evthread_use_pthreads();
    evbase = event_base_new();
    htp = evhtp_new(evbase, NULL);
    evhtp_set_timeouts(htp,&rTimeout,&wTimeout);
    evhtp_use_threads_wexit(htp, threadInitCb, NULL, baseInfo->workThreadNum,(void*)this);
    evSigint = evsignal_new(evbase, SIGINT, sigIntCb, (void*)this);
    evsignal_add(evSigint, NULL);
    evhtp_set_cb(htp, "/", urlCallbackFun, (void*)this);
    evhtp_bind_socket(htp, baseInfo->host.c_str(), baseInfo->port, 1024);
    //代理没有裁决线程
    if (baseInfo->serviceName.find("proxy") != std::string::npos) {
        MimicProxyJudge *that = dynamic_cast<MimicProxyJudge*>(this);
        int count = 0;
        while (true){
            if(that->initThreadRes.size() == 
            this->baseInfo->workThreadNum){
                for(deque<bool>::iterator iter = that->initThreadRes.begin(); 
                iter!=that->initThreadRes.end(); iter++){
                    if(!(*iter)) {
                        logger->error("Threads init error");
                        exit(1);
                    }
                }
                logger->debug("Thead init success, server_name: {}, thread_num: {}", 
                that->baseInfo->serviceName, that->initThreadRes.size());
                break;
            }
            usleep(1000);
            count += 1;
            if (count > 1000*1000) {
                logger->error("Thread init timeout");
                exit(1);
            }
        }
    }
    event_base_loop(evbase, 0);
}

MimicProxyJudge::~MimicProxyJudge(){}

void MimicProxyJudge::initProxyJudgeInfo(GlobalInfo_t *globalConf, vector<string> *ignoreKeys, 
ServiceBaseInfo_t *baseInfo)
{
    initBaseInfo(globalConf, baseInfo);
    this->ignoreKeys = ignoreKeys;
    this->mutexInPj = PTHREAD_MUTEX_INITIALIZER;
}

void MimicProxyJudge::subscribeJudgeRule(void *arg)
{
    MimicProxyJudge * that = (MimicProxyJudge*) arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;

    logger->debug("Subscribe redis thread start: {}", that->baseInfo->serviceName.c_str());

}

void MimicProxyJudge::lockInPj()
{
    pthread_mutex_lock(&mutexInPj);
}

void MimicProxyJudge::unlockInPj()
{
    pthread_mutex_unlock(&mutexInPj);
}

void MimicProxyJudge::recordTcpClientResp(string resp, void *arg) {
    logger->info("tcp client resp, task end: {}", resp);
    MimicTcpClient *cli = (MimicTcpClient *) arg;
    delete cli;
}
