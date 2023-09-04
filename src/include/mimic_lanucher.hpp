#ifndef MIMIC_LANUCHER
#define MIMIC_LANUCHER

#include <map>
#include <unistd.h>
#include "yaml-cpp/yaml.h"
#include "spdlog/spdlog.h"

#include "mimic_struct.hpp"
#include "mimic_service.hpp"

class ServiceLanucher
{
public:
    char *proctitle;
    int titlesize;
    vector<string> serviceToLanucher;
private:
    //存储service对象
    vector<MimicService*> mimicServices;
    //配置文件路径
    YAML::Node& config;
    //redis mysql log配置信息
    GlobalInfo_t *globalConf;
    vector<string> *ignoreKeys;
    std::shared_ptr<spdlog::logger> logger;
public:
    ServiceLanucher(std::shared_ptr<spdlog::logger> logger, YAML::Node& config);
    ~ServiceLanucher();
    void initIgnoreKeys(const YAML::Node& ignoreKeysConfig);
    //redis mysql log配置初始化
    void initGlobalCommpoent(const YAML::Node& globalConfig);
    ServiceBaseInfo_t * initBaseInfo(const YAML::Node& cfgParams);
    //调度对象初始化
    void initMimicScheduleService(const YAML::Node& schedulConfig);
    //代理对像初始化
    void initMimicProxyService(const YAML::Node& proxyConfig);
    //裁决对象初始化
    void initMimicJudgeService(const YAML::Node& judgeConfig);
    void initMimicExecInfo(MimicService *service);
    //解析配置文件生成服务对象
    void initMimicServicesByCfg();
    //多进程服务启动
    void multiServicesLanuch();
};

#endif