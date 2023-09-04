#ifndef MIMIC_UTIL
#define MIMIC_UTIL

#include <string>
#include "mimic_struct.hpp"
#include "spdlog/spdlog.h"
#include "mimic_service.hpp"

#include "/usr/include/mysql/mysql.h"

using namespace std;

namespace MimicUtil{
    void execCmd(char *ret, int size, char *cmd);
    bool isNumber(const std::string& s);
    uint64_t getRandSeed();
    int tcpRequest(string ip, int port, string reqdata, char *resp);
    static void eventCb(struct bufferevent *bev, short events, void *arg);
    static void readCb(struct bufferevent *bev, void *arg);
    int novaToCinder(int sport);
    int novaToNeutron(int sport);
    int cinderToNova(int sport);
    int neutronToNova(int sport);
    string getMethonString(int enumVal);
    bool tcpSend(string ip, int port, string data, bool isrecv, string sid, std::shared_ptr<spdlog::logger> logger);
};

class MimicCommon
{
private:
    MYSQL *mysql;
    GlobalInfo_t *commonConf;
    std::shared_ptr<spdlog::logger> logger;
    bool isInitSucess;
public:
    MimicCommon(GlobalInfo_t *commonConf, std::shared_ptr<spdlog::logger> logger);
    ~MimicCommon();
    void initMysql();
    void execSql(MysqlExecRes_t& res, string sql);
};

class MimicTcpClient
{
public:
    string ip;
    int port;
    string reqdata;
    string resp;
    string sid;

    bool shouldExitLoop;
    MimicProxyJudge * pj;

    struct timeval rTimeout;
    struct timeval wTimeout;

    struct event_base* base;

    MimicTcpClient(string ip, int port, string reqdata, struct event_base* base, MimicProxyJudge * pj, string sid);
    ~MimicTcpClient();
    int tcpRequest();
    static void eventCb(struct bufferevent *bev, short events, void *arg);
    static void readCb(struct bufferevent *bev, void *arg);
};

#endif