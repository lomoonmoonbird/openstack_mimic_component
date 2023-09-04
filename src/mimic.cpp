#include <string>
#include <malloc.h>
#include <fstream>
#include <sys/types.h>
#include <signal.h>
#include <assert.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex>
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"
#include "yaml-cpp/yaml.h"
#include "json/json.h"

#include "mimic_util.hpp"
#include "mimic_lanucher.hpp"

using namespace std;
using namespace MimicUtil;

extern char** environ;

static inline bool checkCfg(string& fileName) {
    ifstream f(fileName.c_str());
    return f.good();
}

static int isSerExist(string sername)
{
    char ret[8] = {0};
    char cmd[512] = {0};
    sprintf(cmd, "ps -ef |grep mimic_%s|grep -v grep >/dev/null 2>&1;echo $?", 
    sername.c_str());
    execCmd(ret, 8, cmd);
    // cout<<cmd<<endl;
    // cout<<ret<<endl;
    return atoi(ret);
}

static pid_t getPidByName(string name)
{
    char retContent[128] = {0};
    char cmdContent[512] = {0};
    sprintf(cmdContent, "ps -ef |grep mimic_%s|grep -v grep|awk '{print $2}' 2>/dev/null", 
    name.c_str());
    execCmd(retContent, 128, cmdContent);
    // cout<<cmdContent<<endl;
    // cout<<retContent<<endl;
    return atoi(retContent);
}

static void printHelpInfo() {
    cout << "\nNAME:\n"
         << "  mimic cloud services\n\n"
         << "USAGE:\n"
         << "  mimic-cloud command [arguments...]\n\n"
         << "COMMANDS:\n"
         << "  start       Start servic, default all\n"
         << "  status      Show service status\n"
         << "  stop        Stop service, default all\n"
         << "  restart     Restart service, default all\n"
         << "  upexecs     up exec list\n"
         << "  execstats   exec status list\n"
         << "  hung-down   stop active exec change\n"
         << "  hung-up     start active exec change\n\n"
         << "EXAMPLE:\n"
         << "  mimic-cloud status\n"
         << "  mimic-cloud start\n"
         << "  mimic-cloud start proxy_horizon schedule_horizon\n"
         << "  mimic-cloud stop\n"
         << "  mimic-cloud stop judge_horizon schedule_nova\n"
         << "  mimic-cloud upexecs\n"
         << "  mimic-cloud execstats\n"
         << "  mimic-cloud hung-up schedule_horizon\n"
         << "  mimic-cloud hung-up\n"
         << "  mimic-cloud hung-down schedule_horizon\n"
         << "  mimic-cloud hung-down\n\n"
         << "GLOBAL OPTIONS:\n"
         << "  --help, -h     show help\n\n"
         << std::endl;
}

static void fillIpportList(map<string, string>& iplist, map<string, int>& portlist, 
YAML::Node& config) {
    iplist["schedule_horizon"] = config["global"]["schedule_horizon"]["ip"].as<std::string>();
    iplist["schedule_controller"] = config["global"]["schedule_controller"]["ip"].as<std::string>();
    iplist["schedule_keystone"] = config["global"]["schedule_keystone"]["ip"].as<std::string>();
    portlist["schedule_horizon"] = config["global"]["schedule_horizon"]["port"].as<int>();
    portlist["schedule_controller"] = config["global"]["schedule_controller"]["port"].as<int>();
    portlist["schedule_keystone"] = config["global"]["schedule_keystone"]["port"].as<int>();
}

static void doQueryInfo(string queryType, YAML::Node& config) {
    map<string, string> iplist;
    map<string, int> portlist;
    fillIpportList(iplist, portlist, config);
    for(auto iteriplist = iplist.begin(); iteriplist != iplist.end(); iteriplist++) {
        int port = portlist[iteriplist->first];
        string ip = iteriplist->second;

        Json::Value reqdata;
        Json::FastWriter jsonWriter;
        reqdata["mode"] = "query_" + queryType;

        MimicTcpClient client(ip, port, jsonWriter.write(reqdata), NULL, NULL, "");
        client.tcpRequest();

        Json::Reader reader;
        Json::Value value;
        try {
            reader.parse(client.resp, value);
        } catch (...) {
            cout << "json parse error" << endl;
            return;
        }

        try {
            if(value["status"].asString() == "ok") {
                value = value["msg"];
            } else {
                cout << "error" << endl;
                return;
            }
        } catch (...) {
            cout << "error" << endl;
            return;
        }

        string ser;
        std::regex pattern("schedule_(.*)");
        std::smatch results;
        string service = iteriplist->first;
        if(std::regex_match(service, results, pattern)) {
            ser = results[1];
        }

        if (value.isArray()) {
            if(ser == "controller") {
                cout << "nova" << endl;
                for(int j = 0; j < value.size(); j++) {
                    cout << value[j].asString() << endl;
                }
                
                cout << "cinder" << endl;
                for(int j = 0; j < value.size(); j++) {
                    int cinderPort;
                    string port;
                    string ip;
                    std::regex pattern("(.*):(.*)");
                    std::smatch results;
                    string addr = value[j].asString();
                    if(std::regex_match(addr, results, pattern)) {
                        port = results[2];
                        ip = results[1];
                    }
                    switch (atoi(port.c_str())) {
                        case 18774:
                            cinderPort = 18776;
                            break;
                        case 28774:
                            cinderPort = 28776;
                            break;
                        case 38774:
                            cinderPort = 38776;
                            break;
                        default:
                            break;
                    }
                    cout << ip + ":" + std::to_string(cinderPort) << endl;
                }

                cout << "neutron" << endl;
                for(int j = 0; j < value.size(); j++) {
                    int neutronPort;
                    string port;
                    string ip;
                    std::regex pattern("(.*):(.*)");
                    std::smatch results;
                    string addr = value[j].asString();
                    if(std::regex_match(addr, results, pattern)) {
                        port = results[2];
                        ip = results[1];
                    }
                    switch (atoi(port.c_str())) {
                        case 18774:
                            neutronPort = 19696;
                            break;
                        case 28774:
                            neutronPort = 29696;
                            break;
                        case 38774:
                            neutronPort = 39696;
                            break;
                        default:
                            break;
                    }
                    cout << ip + ":" + std::to_string(neutronPort) << endl;
                }
            } else {
                cout << ser << endl;
                for(int j = 0; j < value.size(); j++) {
                    cout << value[j].asString() << endl;
                }
            }

        } else if (value.isObject()) {
            Json::Value::Members members = value.getMemberNames();
            if(ser == "controller") {
                cout << "nova" << endl;
                for(auto iter = members.begin(); iter != members.end(); iter++) {
                    string key = *iter;
                    cout << key + "       " << value[key].asString() << endl;
                }

                cout << "cinder" << endl;
                for(auto iter = members.begin(); iter != members.end(); iter++) {
                    string key = *iter;
                    int cinderPort;
                    string port;
                    string ip;
                    std::regex pattern("(.*):(.*)");
                    std::smatch results;
                    if(std::regex_match(key, results, pattern)) {
                        port = results[2];
                        ip = results[1];
                    }
                    switch (atoi(port.c_str())) {
                        case 18774:
                            cinderPort = 18776;
                            break;
                        case 28774:
                            cinderPort = 28776;
                            break;
                        case 38774:
                            cinderPort = 38776;
                            break;
                        default:
                            break;
                    }
                    cout << ip + ":" + std::to_string(cinderPort) + "       " << value[key].asString() << endl;
                }

                cout << "neutron" << endl;
                for(auto iter = members.begin(); iter != members.end(); iter++) {
                    string key = *iter;
                    int neutronPort;
                    string port;
                    string ip;
                    std::regex pattern("(.*):(.*)");
                    std::smatch results;
                    if(std::regex_match(key, results, pattern)) {
                        port = results[2];
                        ip = results[1];
                    }
                    switch (atoi(port.c_str())) {
                        case 18774:
                            neutronPort = 18776;
                            break;
                        case 28774:
                            neutronPort = 28776;
                            break;
                        case 38774:
                            neutronPort = 38776;
                            break;
                        default:
                            break;
                    }
                    cout << ip + ":" + std::to_string(neutronPort) + "       " << value[key].asString() << endl;
                }
            } else {
                cout << ser << endl;
                for(auto iter = members.begin(); iter != members.end(); iter++) {
                    string key = *iter;
                    cout << key + "       " << value[key].asString() << endl;
                }
            }
        } else {
            cout << "return body not array or obj" << endl;
        }
    }
}

static int printSerStatus(vector<string>& servicesEnable)
{
    map<string, string> status;
    for (auto iter=servicesEnable.begin(); iter!=servicesEnable.end();iter++){
        if(isSerExist(*iter)) status.emplace(*iter, "inactive");
        else status.emplace(*iter, "active");
    }
    for(auto it=status.begin();it!=status.end();it++){
        cout << (*it).first + ":      " + (*it).second << endl;
    }
    return 0;
}

static bool isEnable(vector<string>& servicesEnable, string serName)
{
    for(auto iter=servicesEnable.begin(); iter!=servicesEnable.end(); 
    iter++){
        if((*iter) == serName) return true;
    }
    return false;
}

static void getEnalbeSerFromConfig(vector<string>& servicesEnable, 
YAML::Node& config){
    string serGroupName[] = {"schedule", "proxy", "judge"};
    for (int i=0; i<3; i++) {
        for(unsigned index = 0; index < config[serGroupName[i]].size(); index++){
            if(config[serGroupName[i]][index]["enable"].as<int>() != 1) continue;
            servicesEnable.emplace_back(
                config[serGroupName[i]][index]["name"].as<std::string>());
        }
    }
}

static void start(ServiceLanucher *lanucher, vector<string>& servicesEnable, 
int argc, const char *argv[], std::shared_ptr<spdlog::logger> logger)
{
    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (isSerExist(argv[i]) && 
            isEnable(servicesEnable, argv[i])) {
                lanucher->serviceToLanucher.push_back(argv[i]);
            } else {
                logger->debug("Service {} is already active or disable", argv[i]);
            }
        }
    } else {
        for(auto iter=servicesEnable.begin(); 
        iter!=servicesEnable.end(); iter++){
                if(isSerExist(*iter)) lanucher->serviceToLanucher.push_back(*iter);
                else logger->debug("Service {} is already active", *iter);
        }
    }
}

static int stop(vector<string>& servicesEnable, 
int argc, const char *argv[], std::shared_ptr<spdlog::logger> logger)
{
    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!isSerExist(argv[i]) && 
            isEnable(servicesEnable, argv[i])) {
                pid_t pid = getPidByName(argv[i]);
                if(0 != kill(pid, 2)) kill(pid, 9);
                logger->debug("Service {} is stoped", argv[i]);
            } else {
                logger->debug("Service {} is inactive or disable", argv[i]);
            }
        }
    } else {
        for(auto iter = servicesEnable.begin(); 
        iter!=servicesEnable.end(); iter++){
            if (!isSerExist(*iter)) {
                pid_t pid = getPidByName(*iter);
                if(0 != kill(pid, 2)) kill(pid, 9);
                logger->debug("Service {} is stoped", *iter);
            } else {
                logger->debug("Service {} is inactive", *iter);
            }
        }
    }
    return 0;
}

static void hungUpDown(int argc, const char *argv[], YAML::Node& config, 
std::shared_ptr<spdlog::logger> logger, string opt) {
    map<string, string> iplist;
    map<string, int> portlist;
    fillIpportList(iplist, portlist, config);
    if(argc == 2) {
        for(auto iteriplist = iplist.begin(); iteriplist != iplist.end(); iteriplist++) {
            string serName = iteriplist->first;
            if (!isSerExist(serName)){
                int port = portlist[iteriplist->first];
                string ip = iteriplist->second;

                Json::Value valueinner;
                Json::Value valueouter;
                Json::FastWriter jsonwriter;
                valueinner["service"] = serName;
                valueouter["mode"] = "manual_hung-" + opt;
                valueouter["data"] = valueinner;
                string reqdata = jsonwriter.write(valueouter);

                MimicTcpClient client = MimicTcpClient(ip, port, reqdata, NULL, NULL, "");
                client.tcpRequest();

                Json::Reader reader;
                Json::Value value;
                try {
                    reader.parse(client.resp, value);
                } catch (...) {
                    cout << "json parse error" << endl;
                    return;
                }

                if(value["status"].asString() != "ok") {
                    cout << serName + " hung " + opt + " error" << endl;
                } else {
                    cout << serName + " hung " + opt + " ok" << endl;
                }
            } else {
                cout << serName + " not runing" << endl;
            }
        }
    } else {
        vector<string> serList;
        for(int j = 0; j < config["schedule"].size(); j++) {
            serList.emplace_back(config["schedule"][j]["name"].as<std::string>());
        }
        for (int i = 2; i < argc; i++) {
            string serName = argv[i];
            if(std::find(serList.begin(), serList.end(), serName) != serList.end()) {
                if (!isSerExist(serName)) {
                    Json::Value valueinner;
                    Json::Value valueouter;
                    Json::FastWriter jsonwriter;
                    valueinner["service"] = serName;
                    valueouter["mode"] = "manual_hung-" + opt;
                    valueouter["data"] = valueinner;
                    string reqdata = jsonwriter.write(valueouter);

                    string ip;
                    int port;
                    for(int k = 0; k < config["schedule"].size(); k++) {
                        if(config["schedule"][k]["name"].as<std::string>() == serName) {
                            ip = iplist[serName];
                            port = portlist[serName];
                            break;
                        }
                    }

                    MimicTcpClient client = MimicTcpClient(ip, port, reqdata, NULL, NULL, "");
                    client.tcpRequest();

                    Json::Reader reader;
                    Json::Value value;
                    try {
                        reader.parse(client.resp, value);
                    } catch (...) {
                        cout << "json parse error" << endl;
                        return;
                    }

                    if(value["status"].asString() != "ok") {
                        cout << serName + " hung " + opt + " error" << endl;
                    } else {
                        cout << serName + " hung " + opt + " ok" << endl;
                    }
                } else {
                    cout << serName + " not runing" << endl;
                }
            } else {
                cout << serName + " not int config" << endl;
            }
        }
    }
}

int main(int argc, const char *argv[]) {
    if(argc < 2) {
        cout << "Invalid Input"<<endl;
        exit(1);
    }
    std::shared_ptr<spdlog::logger> logger = spdlog::stdout_color_mt("console");
    logger->set_pattern("[%H:%M:%S] %v");
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::debug);
    // spdlog::flush_every(std::chrono::seconds(1));

    int titlesize = 0;
    for(int i=0;i<argc;i++){
        titlesize = titlesize+strlen(argv[i])+1;
    }
    for(int j=0;environ[j];j++){
        titlesize = titlesize+strlen(environ[j])+1;
    }

    string cfgPath = "/etc/mimic-cloud/mimic_cloud.yml";
    assert(checkCfg(cfgPath));
    YAML::Node config;
    try{
        config = YAML::LoadFile(cfgPath.c_str());
    } catch (...) {
        logger->debug("Load config fail");
        exit(1);
    }
    vector<string> servicesEnable;
    getEnalbeSerFromConfig(servicesEnable, config);

    ServiceLanucher *lanucher = new ServiceLanucher(logger, config);
    lanucher->proctitle = const_cast<char*>(argv[0]);
    lanucher->titlesize = titlesize;
    if (!strcmp(argv[1], "status")) {
        if(argc > 2) {
            logger->debug("Invalid Input");
            return 1;
        }
        return printSerStatus(servicesEnable);
    } else if (!strcmp(argv[1], "upexecs")) {
        if(argc > 2) {
            logger->debug("Invalid Input");
            return 1;
        }
        doQueryInfo("upexecs", config);
        return 0;
    } else if (!strcmp(argv[1], "execstats")) {
        if(argc > 2) {
            logger->debug("Invalid Input");
            return 1;
        }
        doQueryInfo("execstatus", config);
        return 0;
    } else if (!strcmp(argv[1], "hung-up")) {
        hungUpDown(argc, argv, config, logger, "up");
        return 0;
    } else if (!strcmp(argv[1], "hung-down")) {
        hungUpDown(argc, argv, config, logger, "down");
        return 0;
    } else if (!strcmp(argv[1], "start")) {
        // logger->debug("Lanucher services start");
        start(lanucher, servicesEnable, argc, argv, logger);
        if (lanucher->serviceToLanucher.empty()) return 0;
    } else if (!strcmp(argv[1], "stop")) {
        return stop(servicesEnable, argc, argv, logger);
    } else if (!strcmp(argv[1], "restart")) {
        stop(servicesEnable, argc, argv, logger);
        usleep(200000);
        // logger->debug("Lanucher services start");
        start(lanucher, servicesEnable, argc, argv, logger);
        if (lanucher->serviceToLanucher.empty()) return 0;
    } else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        printHelpInfo();
        return 0;
    } else {
        cout << "Invalid Input" << endl;
        exit(1);
    }

    lanucher->initMimicServicesByCfg();
    lanucher->multiServicesLanuch(); 

    return 0;
}

