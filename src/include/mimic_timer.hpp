#ifndef MIMIC_TIMER
#define MIMIC_TIMER

#include <pthread.h>
#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/util.h>

#include "spdlog/spdlog.h"
#include "mimic_struct.hpp"
#include "mimic_service.hpp"

class MimicTimer
{
private:
    string timerName;
    int isPersistent;
    struct event timer;
    struct timeval tv;
    MimicService *service;
    pthread_t ntid;

    std::shared_ptr<spdlog::logger> logger;
public:
    bool isRuning;
    struct event_base *base;
    int second;
    MimicTimer(string timerName, int isPersistent, int second, MimicService *service);
    ~MimicTimer();
    int timerSpawn();
    int timerStop();
    int timerRun();
    static void *threadStart(void *arg);
    static void timerCb(evutil_socket_t fd, short event, void *arg);
};

#endif
