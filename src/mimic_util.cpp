#include <iostream>
#include <cstring>
#include <regex>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "/usr/include/mysql/mysql.h"
#include "mimic_util.hpp"
#include "json/json.h"

using namespace std;

/**
 * @brief 判断字符串是否是数字
 * 
 * @param s 要判断的字符串
 * 
 * @return 是数字返回true，否则返回false
*/
bool MimicUtil::isNumber(const std::string& s) {
    std::istringstream iss(s);
    double dummy;
    return iss >> dummy && iss.eof();
}

uint64_t MimicUtil::getRandSeed() {
    struct timeval seedtime;
    gettimeofday(&seedtime, NULL);
    uint64_t seed = seedtime.tv_sec*1000000 + seedtime.tv_usec;
    return seed;
}

void MimicUtil::execCmd(char *ret, int size, char *cmd)
{
    FILE *fp = NULL;
    fp = popen(cmd, "r");
    if (fp == NULL)
    {
        return;
    }
    fread(ret, sizeof(char), size, fp);
    pclose(fp);
}

MimicCommon::MimicCommon(GlobalInfo_t *commonConf,
std::shared_ptr<spdlog::logger> logger):commonConf(commonConf),logger(logger){
    this->mysql = nullptr;
    this->isInitSucess = false;
}

MimicCommon::~MimicCommon(){
    if(mysql) mysql_close(mysql);
}

void MimicCommon::initMysql()
{
    char reconnect = 1;

    mysql = mysql_init(NULL);

    if (!mysql_real_connect(mysql, 
    commonConf->mysqlHost.c_str(), 
    commonConf->mysqlUser.c_str(), 
    commonConf->mysqlPassword.c_str(), 
    commonConf->mysqlDb.c_str(), 
    commonConf->mysqlPort, NULL, 0)){
        logger->error("Connect mysql error: {}, {}, {}, {}, {}", 
        commonConf->mysqlHost.c_str(), 
        commonConf->mysqlUser.c_str(), 
        commonConf->mysqlPassword.c_str(), 
        commonConf->mysqlDb.c_str(), 
        commonConf->mysqlPort);
        throw "Mysql Init EROR";

    }else{
        mysql_set_character_set(mysql, "utf8");
        mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect);
        isInitSucess = true;
    }
    /*
    if (mysql_ping(mysql)){
        logger->error("Connected but ping fail: {}, {}, {}, {}, {}",
        commonConf->mysqlHost.c_str(), 
        commonConf->mysqlUser.c_str(), 
        commonConf->mysqlPassword.c_str(), 
        commonConf->mysqlDb.c_str(), 
        commonConf->mysqlPort);
        // throw "Mysql Init EROR";
    }
    */
    logger->info("Connect mysql success");
    /**test
    MysqlExecRes_t res1;
    this->execSql(res1, "select * from exec_status_info;");
    logger->info("sql1res: {}, {}", res1.isSelect, res1.isSuccess);
    for(auto iter = res1.result.begin(); iter!=res1.result.end(); iter++) {
        for(auto iter2 = (*iter).begin(); iter2!=(*iter).end(); iter2++){
            // logger->info("k: {}, v: {}", (*iter2).first, (*iter2).second);
        }
    }
    MysqlExecRes_t res2;
    string sql = "INSERT INTO judge_log_info SET execs=\"test\",error_exc=\"2\",error_reason=\"test\",msg_type=\"test\",detail_result=\"test\",judge_strategy=\"test\",create_time= now();";
    this->execSql(res2, sql);
    logger->info("sql2res: {}, {}", res2.isSelect, res2.isSuccess);
    */
}

void MimicCommon::execSql(MysqlExecRes_t& res, string sql)
{
    //重连
    if(!this->isInitSucess) {
        try {
            initMysql();
        } catch(const char* &e) {
            logger->error("mysql Init fail: {}", e);
            res.isSuccess = false;
            return;
        }
    }
    //检测
    if (mysql_ping(mysql)){
        logger->error("mysql ping fail");
        res.isSuccess = false;
        return;
    }
    res.isSelect = false;
    if (mysql_real_query(mysql,sql.c_str(),
    (unsigned int)strlen(sql.c_str()))) {
        res.isSuccess = false;
        logger->error("Exec sql error: {}", mysql_error(mysql));
        return;
    }
    
    MYSQL_RES *result = mysql_store_result(mysql);
    if (result) {
        res.isSelect = true;
        vector<string> names;
        int fieldsNum = mysql_num_fields(result);
        MYSQL_FIELD *fields = mysql_fetch_fields(result);
        for( int i = 0; i < fieldsNum; i++ ) {
            names.push_back(fields[i].name);
        }

        int row_count = mysql_num_rows(result);
        vector<map<string,string>> outer;
        MYSQL_ROW row;
        while (row = mysql_fetch_row(result)) {
            map<string,string> inner;
            for (int i = 0; i < fieldsNum; i++) {
                inner.emplace(names[i], row[i]);
            }
            outer.emplace_back(inner);
        }
        res.result = std::move(outer);
        mysql_free_result(result);
        res.isSuccess = true;
    } else {
        if (mysql_field_count(mysql) == 0) {
            unsigned long long num_rows = mysql_affected_rows(mysql);
            if(num_rows == 0) {
                res.isSuccess = false;
                logger->error("Affect row 0, it is error");
                return;
            }
            res.isSuccess = true;
        } else {
            logger->error("Get result error:: %s", mysql_error(mysql));
            res.isSuccess = false;
            return;
        }
    }
    // if(!res.isSuccess) mysql_close(mysql);
}

MimicTcpClient::MimicTcpClient(string ip, int port, string reqdata, struct event_base* base, 
MimicProxyJudge * pj, string sid):ip(ip), port(port), reqdata(reqdata), base(base), pj(pj), sid(sid) {
    memset(&rTimeout,0x00,sizeof(rTimeout));
    memset(&wTimeout,0x00,sizeof(wTimeout));
    this->rTimeout.tv_sec = 5;
    this->wTimeout.tv_sec = 5;
    shouldExitLoop = false;
    resp = "default_resp";
}

MimicTcpClient::~MimicTcpClient(){}

void MimicTcpClient::readCb(struct bufferevent *bev, void *arg) 
{
    MimicTcpClient *cli = (MimicTcpClient*)arg;
    while (true) {
        struct evbuffer *input = bufferevent_get_input(bev);
        size_t sz = evbuffer_get_length(input);
        if(sz == 0) {
            bufferevent_flush(bev, EV_READ, BEV_NORMAL);
            break;
        }

        char *line = evbuffer_readln(input, NULL, EVBUFFER_EOL_CRLF);
        if(line == NULL || strlen(line) == 0) {
            if(line != NULL || strlen(line) == 0) free(line);
            bufferevent_flush(bev, EV_READ, BEV_NORMAL);
            bufferevent_free(bev);
            break;
        }
        cli->resp = string(line);
        free(line);
    }

    if(cli->pj != NULL)
        cli->pj->logger->debug("read cb data: {}, sid: {}", 
        cli->resp, cli->sid.empty() ? "nosid" : cli->sid);

    if(cli->shouldExitLoop) {
        event_base_loopexit(cli->base, NULL);
        return;
    }
    if(cli->pj != NULL) 
        cli->pj->recordTcpClientResp(cli->resp, cli);
}
 
void MimicTcpClient::eventCb(struct bufferevent *bev, short events, void *arg)
{
    MimicTcpClient *cli = (MimicTcpClient*)arg;
    if (events & BEV_EVENT_EOF) {
        if(cli->pj != NULL)
            cli->pj->logger->debug("eof event, sid: {}", 
            cli->sid.empty() ? "nosid" : cli->sid);
        
        bufferevent_free(bev);

        if(cli->shouldExitLoop) {
            event_base_loopexit(cli->base, NULL);
            return;
        }
        
        if(cli->pj != NULL) 
            cli->pj->recordTcpClientResp(cli->resp, cli);
    } else if(events & BEV_EVENT_ERROR) {
        if(cli->pj != NULL)
            cli->pj->logger->debug("error event, sid: {}", 
            cli->sid.empty() ? "nosid" : cli->sid);
        
        bufferevent_free(bev);

        if(cli->shouldExitLoop) {
            event_base_loopexit(cli->base, NULL);
            return;
        }
        
        if(cli->pj != NULL) 
            cli->pj->recordTcpClientResp(cli->resp, cli);
    } else if(events & BEV_EVENT_TIMEOUT) {
        if(cli->pj != NULL)
            cli->pj->logger->debug("timeout event, sid: {}", 
            cli->sid.empty() ? "nosid" : cli->sid);
        
        bufferevent_free(bev);

        if(cli->shouldExitLoop) {
            event_base_loopexit(cli->base, NULL);
            return;
        }
        
        if(cli->pj != NULL)
            cli->pj->recordTcpClientResp(cli->resp, cli);
    } else if(events & BEV_EVENT_CONNECTED) {
        if(cli->pj != NULL)
            cli->pj->logger->debug("remote connected");
        char endbuf[2] = {'\r', '\n'};
        bufferevent_write(bev, cli->reqdata.c_str(), strlen(cli->reqdata.c_str()));
        bufferevent_write(bev, endbuf, 2);
        bufferevent_flush(bev, EV_WRITE, BEV_NORMAL);

        if(cli->pj != NULL)
            cli->pj->logger->debug("sid, {}, sended data : {}", 
            cli->sid.empty() ? "nosid" : cli->sid, cli->reqdata);
    }
}

int MimicTcpClient::tcpRequest() 
{
    if(base == NULL) {
        base = event_base_new();
        shouldExitLoop = true;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
 
    struct bufferevent* bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
 
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;

    serv.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &serv.sin_addr.s_addr);
 
    bufferevent_socket_connect(bev, (struct sockaddr*)&serv, sizeof(serv));
    bufferevent_setcb(bev, MimicTcpClient::readCb, NULL, MimicTcpClient::eventCb, this);
    bufferevent_enable(bev, EV_READ);
    bufferevent_enable(bev, EV_WRITE);
    bufferevent_set_timeouts(bev, &rTimeout, &wTimeout);

    if(shouldExitLoop) {
        event_base_dispatch(base);
        event_base_free(base);
    }

    return 0;
}

int MimicUtil::novaToCinder(int sport){
    map<int, int> relateMap;
    relateMap[18774] = 18776;
    relateMap[28774] = 28776;
    relateMap[38774] = 38776;
    return relateMap[sport];
}

int MimicUtil::novaToNeutron(int sport){
    map<int, int> relateMap;
    relateMap[18774] = 19696;
    relateMap[28774] = 29696;
    relateMap[38774] = 39696;
    return relateMap[sport];
}

int MimicUtil::cinderToNova(int sport){
    map<int, int> relateMap;
    relateMap[18776] = 18774;
    relateMap[28776] = 28774;
    relateMap[38776] = 38774;
    return relateMap[sport];
}

int MimicUtil::neutronToNova(int sport) {
    map<int, int> relateMap;
    relateMap[19696] = 18774;
    relateMap[29696] = 28774;
    relateMap[39696] = 38774;
    return relateMap[sport];
}

string MimicUtil::getMethonString(int enumVal) {
    string ret = "default_methond";
    switch (enumVal)
    {
    case 0:
        ret = "GET";
        break;
    case 1:
        ret = "HEAD";
        break;
    case 2:
        ret = "POST";
        break;
    case 3:
        ret = "PUT";
        break;
    case 4:
        ret = "DELETE";
        break;
    case 5:
        ret = "MKCOL";
        break;
    case 6:
        ret = "COPY";
        break;
    case 7:
        ret = "MOVE";
        break;
    case 8:
        ret = "OPTIONS";
        break;
    case 9:
        ret = "PROPFIND";
        break;
    case 10:
        ret = "PROPPATCH";
        break;
    case 11:
        ret = "LOCK";
        break;
    case 12:
        ret = "UNLOCK";
        break;
    case 13:
        ret = "TRACE";
        break;
    case 14:
        ret = "CONNECT,";
        break;
    case 15:
        ret = "PATCH";
        break;
    case 16:
        ret = "UNKNOWN";
        break;
    default:
        break;
    }
    return ret;
}

bool MimicUtil::tcpSend(string ip, int port, string data, bool isrecv, string sid, std::shared_ptr<spdlog::logger> logger)
{
    int socket_fd;
    int ret = -1;
    struct sockaddr_in serveraddr;
    string resp;

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(socket_fd==-1){
        logger->debug("create client fd fail, sid: {}", sid);
        close(socket_fd);
        return false;
    }
    
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = port;
    serveraddr.sin_addr.s_addr = inet_addr(ip.c_str());
    ret = connect(socket_fd,(struct sockaddr *)&serveraddr,sizeof(serveraddr));
    if(ret<0){
        logger->debug("faile to connect server, ip: {}, port: {} sid: {}", ip, port, sid);
        close(socket_fd);
        return false;
    }

    ret = send(socket_fd,data.c_str(),strlen(data.c_str()),0);
    if(ret<=0){
        logger->debug("fail to send data, sid: {}", sid);
        close(socket_fd);
        return false;
    } else {
        logger->debug("sid: {}, send data ok: {}", sid, data);
    }

    if (isrecv) {
        while(1) {
            char recvbuf[1024] = {0};
            ret = recv(socket_fd, recvbuf, 1024, 0);
            logger->debug("recv data: {}", recvbuf);
            if (strlen(recvbuf) > 0) {
                resp += string(recvbuf);
            }
            if(ret < 1024) {
                logger->debug("recv end, sid: {}", sid);
                break;
            }
        }
    }

    close(socket_fd);
    return true;
}
