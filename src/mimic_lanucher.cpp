#include <iostream>
#include <unistd.h>
#include <regex>

#include "mimic_lanucher.hpp"
#include "mimic_proxy.hpp"
#include "mimic_judge.hpp"
#include "mimic_schedule.hpp"
#include "spdlog/spdlog.h"

ServiceLanucher::ServiceLanucher(std::shared_ptr<spdlog::logger> logger, 
YAML::Node& config):logger(logger), config(config) {
    this->globalConf = nullptr;
    this->ignoreKeys = nullptr;
}

ServiceLanucher::~ServiceLanucher()
{
    for(vector<MimicService*>::iterator iter = mimicServices.begin(); 
    iter != mimicServices.end(); iter++){
        if((*iter) != nullptr) {
            delete *iter;
            *iter = nullptr;
        }
    }
    if(globalConf != nullptr) {
        delete globalConf;
        globalConf = nullptr;
    }
    if(ignoreKeys != nullptr) {
        delete ignoreKeys;
        ignoreKeys = nullptr;
    }
}

void ServiceLanucher::initIgnoreKeys(const YAML::Node& ignoreKeysConfig)
{
    logger->debug("Init ignoreKeys");
    vector<string> *ignoreKeysList = new vector<string>;
    for(unsigned index = 0; index < ignoreKeysConfig.size(); index++){
        ignoreKeysList->emplace_back(ignoreKeysConfig[index].as<std::string>());
    }
    this->ignoreKeys = ignoreKeysList;
}

void ServiceLanucher::initGlobalCommpoent(const YAML::Node& globalConfig) 
{
    logger->debug("Init global commpoent baseInfo");
    globalConf = new GlobalInfo_t;
    
    globalConf->execNum = globalConfig["exec_num"].as<int>();
    globalConf->mysqlHost = globalConfig["mysql"]["host"].as<std::string>();
    globalConf->mysqlPort = globalConfig["mysql"]["port"].as<int>();
    globalConf->mysqlUser = globalConfig["mysql"]["user"].as<std::string>();
    globalConf->mysqlPassword = globalConfig["mysql"]["passwd"].as<std::string>();
    globalConf->mysqlDb = globalConfig["mysql"]["mysqldb"].as<std::string>();

    globalConf->schedule_horizon_ip = globalConfig["schedule_horizon"]["ip"].as<std::string>();
    globalConf->schedule_horizon_port = globalConfig["schedule_horizon"]["port"].as<int>();

    globalConf->schedule_controller_ip = globalConfig["schedule_controller"]["ip"].as<std::string>();
    globalConf->schedule_controller_port = globalConfig["schedule_controller"]["port"].as<int>();

    globalConf->schedule_keystone_ip = globalConfig["schedule_keystone"]["ip"].as<std::string>();
    globalConf->schedule_keystone_port = globalConfig["schedule_keystone"]["port"].as<int>();
}

ServiceBaseInfo_t *ServiceLanucher::initBaseInfo(const YAML::Node& cfgParams)
{
    ServiceBaseInfo_t *baseInfo = new ServiceBaseInfo_t;

    baseInfo->serviceName = cfgParams["name"].as<std::string>();
    baseInfo->host = cfgParams["bind"].as<std::string>();
    baseInfo->port = cfgParams["port"].as<int>();
    string::size_type position = baseInfo->serviceName.find("schedule");
    if (position != string::npos) {
        baseInfo->workThreadNum = 0;
        baseInfo->reportInterval = cfgParams["report_interval"].as<int>();
    } else {
        baseInfo->workThreadNum = cfgParams["work_threads"].as<int>();
    }
    baseInfo->timeout = cfgParams["timeout"].as<int>();
    return baseInfo;
}

void ServiceLanucher::initMimicScheduleService(const YAML::Node& scheduleConfig) 
{
    logger->debug("Init schedule servies");
    for(unsigned index = 0; index < scheduleConfig.size(); index++) {
        if(scheduleConfig[index]["enable"].as<int>() != 1) continue;
        for (auto iter = serviceToLanucher.begin(); iter!=serviceToLanucher.end(); iter++) {
            if ((*iter) == scheduleConfig[index]["name"].as<std::string>()) {
                ServiceBaseInfo_t *baseInfo = initBaseInfo(scheduleConfig[index]);
                MimicSchedule *mimicSchedule = new MimicSchedule(globalConf, baseInfo);
                initMimicExecInfo(mimicSchedule);
                // mimicSchedule->initLogger();
                mimicSchedule->registCallbacks();
                mimicServices.emplace_back(mimicSchedule);
            }
        }
    }
}

void ServiceLanucher::initMimicProxyService(const YAML::Node& proxyConfig) 
{
    logger->debug("Init proxy services");
    for(unsigned index = 0; index < proxyConfig.size(); index++) {
        if(proxyConfig[index]["enable"].as<int>() != 1) continue;
        for (auto iter = serviceToLanucher.begin(); iter!=serviceToLanucher.end(); iter++) {
            if ((*iter) == proxyConfig[index]["name"].as<std::string>()) {
                ServiceBaseInfo_t *baseInfo = initBaseInfo(proxyConfig[index]);
                MimicProxy *mimicProxy = new MimicProxy(globalConf, ignoreKeys, baseInfo);
                initMimicExecInfo(mimicProxy);
                // mimicProxy->initLogger();
                mimicProxy->registCallbacks();
                mimicServices.emplace_back(mimicProxy);
            }
        }
    }
}

void ServiceLanucher::initMimicJudgeService(const YAML::Node& judgeConfig) 
{
    logger->debug("Init judge services");
    for(unsigned index = 0; index < judgeConfig.size(); index++) {
        if(judgeConfig[index]["enable"].as<int>() != 1) continue;
        for (auto iter = serviceToLanucher.begin(); iter!=serviceToLanucher.end(); iter++) {
            if ((*iter) == judgeConfig[index]["name"].as<std::string>()) {
                ServiceBaseInfo_t *baseInfo = initBaseInfo(judgeConfig[index]);
                MimicJudge *mimicJudge = new MimicJudge(globalConf, ignoreKeys, baseInfo);
                // mimicJudge->initLogger();
                initMimicExecInfo(mimicJudge);
                mimicJudge->registCallbacks();
                mimicServices.emplace_back(mimicJudge);
            }
        }
    }
}

void ServiceLanucher::initMimicExecInfo(MimicService *service)
{
    string::size_type position;
    position = service->baseInfo->serviceName.find("horizon");
    if (position != string::npos) {
        for(unsigned index = 0; index < config["exec-horizon"].size(); index++){
            string web;
            if(config["exec-hoirzon"][index]["web"].IsNull()) web = "";
            else web = config["exec-horizon"][index]["web"].as<std::string>();
            execBaseInfo_t execBase = {
                config["exec-horizon"][index]["ip"].as<std::string>(),
                config["exec-horizon"][index]["port"].as<int>(),
                config["exec-horizon"][index]["os"].as<std::string>(),
                web,
                config["exec-horizon"][index]["arch"].as<std::string>()
            };
            service->execInfo.emplace_back(execBase);
        }
    }
    position = service->baseInfo->serviceName.find("controller");
    if (position != string::npos) {
        for(unsigned index = 0; index < config["exec-controller"].size(); index++){
            string web;
            if(config["exec-controller"][index]["web"].IsNull()) web = "";
            else web = config["exec-controller"][index]["web"].as<std::string>();
            execBaseInfo_t execBase = {
                config["exec-controller"][index]["ip"].as<std::string>(),
                config["exec-controller"][index]["port"].as<int>(),
                config["exec-controller"][index]["os"].as<std::string>(),
                web,
                config["exec-controller"][index]["arch"].as<std::string>()
            };
            service->execInfo.emplace_back(execBase);
        }
    }
    position = service->baseInfo->serviceName.find("keystone");
    if (position != string::npos) {
        for(unsigned index = 0; index < config["exec-keystone"].size(); index++){
            string web;
            if(config["exec-keystone"][index]["web"].IsNull()) web = "";
            else web = config["exec-keystone"][index]["web"].as<std::string>();
            execBaseInfo_t execBase = {
                config["exec-keystone"][index]["ip"].as<std::string>(),
                config["exec-keystone"][index]["port"].as<int>(),
                config["exec-keystone"][index]["os"].as<std::string>(),
                web,
                config["exec-keystone"][index]["arch"].as<std::string>()
            };
            service->execInfo.emplace_back(execBase);
        }
    }
}

void ServiceLanucher::initMimicServicesByCfg() 
{
    try{
        bool isIgnoreKeyInit = false;
        bool isInitProxy = false;
        bool isInitJudge = false;
        bool isInitSchedule = false;
        regex judgeRe("^judge_.*$");
        regex proxyRe("^proxy_.*$");
        regex scheduleRe("^schedule_.*$");
        for(auto iter = serviceToLanucher.begin(); iter != serviceToLanucher.end(); iter++) {
            if(regex_match((*iter), judgeRe))  {
                isIgnoreKeyInit = true;
                isInitJudge = true;
            }
            if(regex_match((*iter), proxyRe)) {
                isIgnoreKeyInit = true;
                isInitProxy = true;
            }
            if(regex_match((*iter), scheduleRe)) {
                isInitSchedule = true;
            }
        }

        logger->debug("Init services start");
        initGlobalCommpoent(config["global"]);
        if (isIgnoreKeyInit) {
            initIgnoreKeys(config["not_support_key"]);
        }
        if (isInitSchedule) {
            initMimicScheduleService(config["schedule"]);
        }
        if (isInitProxy) {
            initMimicProxyService(config["proxy"]);
        }
        if (isInitJudge) {
            initMimicJudgeService(config["judge"]);
        }
        logger->debug("Init services end");
    } catch (...) {
        logger->debug("Init services fail");
        exit(1);
    }
}

void ServiceLanucher::multiServicesLanuch() 
{
    for(vector<MimicService*>::iterator itouter=mimicServices.begin(); 
    itouter!=mimicServices.end(); itouter++) {
        if(!fork()) {
            //析构不需要的资源
            bool deleletIgnore = false;
            bool isScheduleService = false;
            string::size_type position = (*itouter)->baseInfo->serviceName.find("schedule");
            if (position != string::npos) isScheduleService = true;

            for(vector<MimicService*>::iterator itinner=mimicServices.begin(); 
            itinner!=mimicServices.end(); itinner++) {
                if(isScheduleService) {
                    string::size_type positionJudge = (*itinner)->baseInfo->serviceName.find("judge");
                    string::size_type positionProxy = (*itinner)->baseInfo->serviceName.find("proxy");
                    if (positionJudge != string::npos || positionProxy != string::npos) {
                        MimicProxyJudge *servicePtr = dynamic_cast<MimicProxyJudge*>(*itinner);
                        if(servicePtr->ignoreKeys != nullptr){
                            if(!deleletIgnore) {
                                delete servicePtr->ignoreKeys;
                                servicePtr->ignoreKeys = nullptr;
                                this->ignoreKeys = nullptr;
                                deleletIgnore = true;
                            } else {
                                servicePtr->ignoreKeys = nullptr;
                            }
                        }
                    }
                }
                if (itouter != itinner){
                    if((*itinner) != nullptr) {
                        delete *itinner;
                        *itinner = nullptr;
                    }
                }
            }
            //启动孙进程，service只剩下一份
            if(!fork()) {
                memset(proctitle, '\0', titlesize);
                strcpy(proctitle, ("mimic_"+(*itouter)->baseInfo->serviceName).c_str());
                // spdlog file log bug死锁问题子进程不打印日志 采用consolelog
                logger->debug("Spawn service: {}", (*itouter)->baseInfo->serviceName);
                if((*itouter) != nullptr) {
                    (*itouter)->initLogger();
                    (*itouter)->serviceSpawn();
                    break;
                }
                else exit(0);
            } else {
                //退出子进程 系统自动回收资源不手动析构
                exit(0);
            }
        } else {
            if(itouter == mimicServices.end() - 1) {
                // wait lanucher end
                usleep(500000);
                // logger->debug("Exit lanucher process");
                //退出父进程 系统自动回收资源不手动析构
                exit(0);
            }
        }
    }
}


