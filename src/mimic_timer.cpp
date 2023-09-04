#include <iostream>
#include "mimic_timer.hpp"
#include "mimic_schedule.hpp"


MimicTimer::MimicTimer(string timerName, int isPersistent, 
int second, MimicService *service):
    timerName(timerName),
    isPersistent(isPersistent),
    second(second), 
    service(service) 
{
    this->logger = service->logger;
    this->isRuning = false;
}

MimicTimer::~MimicTimer() {
    evutil_timerclear(&tv);
}

void MimicTimer::timerCb(evutil_socket_t fd, short event, void *arg)
{   
    bool isSchedule = false;
    MimicTimer *that = (MimicTimer*)arg;
    std::shared_ptr<spdlog::logger> logger = that->logger;
    MimicService *service = that->service;
    logger->debug("In timercb: {}", that->timerName);

    string::size_type position = service->baseInfo->serviceName.find("schedule");
    if (position != string::npos) isSchedule = true;

    if(isSchedule) {
        MimicSchedule *scheduleSer = dynamic_cast<MimicSchedule*>(service);
        if(that->timerName == "auto_report_timer") {
            scheduleSer->execStatusReport();
        } else if (that->timerName == "auto_schedule_timer") {
            scheduleSer->execAtuoSchedule();
        }
    } else {
        MimicProxyJudge *pjService = dynamic_cast<MimicProxyJudge*>(service);
        //TODO
    }
}

int MimicTimer::timerRun()
{
    int evFlags;
    if (isPersistent) evFlags = EV_PERSIST;
    else evFlags = 0;
    
	base = event_base_new();
	event_assign(&timer, base, -1, evFlags, MimicTimer::timerCb, this);
	evutil_timerclear(&tv);
	tv.tv_sec = second;
    logger->debug("second: {}", second);
	event_add(&timer, &tv);

	event_base_dispatch(base);
    // event_free(&timer);
    event_base_free(base);
	return 0;
}

void *MimicTimer::threadStart(void *arg)
{
    MimicTimer *that = (MimicTimer*)arg;
    that->logger->debug("Run timer: {}", that->timerName);
    that->timerRun();
}

int MimicTimer::timerSpawn()
{   
    logger->debug("Spawn timer: {}", timerName);
    isRuning = true;
    if(0 != pthread_create(&ntid, NULL, MimicTimer::threadStart, this)) return 0;
    else return 1;
    pthread_detach(ntid);
}

int MimicTimer::timerStop()
{
    logger->debug("Stop timer: {}", timerName);
    evutil_timerclear(&tv);

    MimicService *service = this->service;
    bool isSchedule = false;
    string::size_type position = service->baseInfo->serviceName.find("schedule");
    if (position != string::npos) isSchedule = true;
    if(isSchedule) {
        MimicSchedule *scheduleSer = dynamic_cast<MimicSchedule*>(service);
        pthread_mutex_lock(&(scheduleSer->mutexExecs));
        event_base_loopexit(base, NULL);
        pthread_mutex_unlock(&(scheduleSer->mutexExecs));
    }

    event_base_loopexit(base, NULL);
    isRuning = false;
    return 0;
}