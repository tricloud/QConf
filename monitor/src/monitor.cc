#include <thread>
#include <vector>
#include <map>
#include <string>
#include <fstream>
#include <iostream>
#include <cstdio>

#include "monitor_util.h"
#include "monitor_config.h"
#include "monitor_const.h"
#include "monitor_log.h"
#include "monitor_process.h"
#include "monitor_zk.h"
#include "monitor_load_balance.h"
#include "monitor_listener.h"
#include "monitor_work_thread.h"

using namespace std;

Config *p_conf = NULL;
LoadBalance *p_loadBalance = NULL;
ServiceListener *p_serviceListerner = NULL;

int doLoadBalance() {
    int ret = MONITOR_OK;
    if ((ret = p_loadBalance->getMd5ToServiceFather()) != MONITOR_OK) {
        LOG(LOG_ERROR, "get md5 to service father failed");
        /* TODO (gaodq)
         * how to deal with this in a better way?
         * if the reason of failure is node not exist, we should restart main loop
        */
        return ret;
    }

    if ((ret = p_loadBalance->getMonitors()) != MONITOR_OK) {
        LOG(LOG_ERROR, "get monitors failed");
        return ret;
    }

    if ((ret = p_loadBalance->balance()) != MONITOR_OK) {
        LOG(LOG_ERROR, "balance failed");
        return ret;
    }
    return ret;
}

void loadServiceToConf() {
    p_conf->clearService();
    p_serviceListerner->cleanServiceFatherToIp();
    p_serviceListerner->getAllIp();
    p_serviceListerner->loadAllService();
}

int main(int argc, char** argv) {
    int ret = MONITOR_OK;
    p_conf = new Config(CONF_PATH);
    if ((ret = p_conf->Load()) != MONITOR_OK)
        return ret;

    if (Process::isProcessRunning(MONITOR_PROCESS_NAME)) {
        LOG(LOG_ERROR, "Monitor is already running.");
        return MONITOR_ERR_OTHER;
    }
    if (p_conf->isDaemonMode())
        Process::daemonize();

    if (p_conf->isAutoRestart()) {
        int childExitStatus = -1;
        int ret = Process::processKeepalive(childExitStatus, PIDFILE);
        // Parent process
        if (ret > 0)
            return childExitStatus;
        else if (ret < 0)
            return MONITOR_ERR_OTHER;
        else if ((ret = Util::writePid(PIDFILE.c_str())) != MONITOR_OK)
            return ret;
    }

    p_loadBalance = new LoadBalance();
    if ((ret = p_loadBalance->initMonitor()) != MONITOR_OK) {
        LOG(LOG_ERROR, "init load balance env failed");
        return ret;
    }
    p_serviceListerner = new ServiceListener();
    if ((ret = p_serviceListerner->initListener()) != MONITOR_OK) {
        LOG(LOG_ERROR, "init service listener env failed");
        return ret;
    }

    /*
     * this loop is for load balance.
     * If rebalance is needed, the loop will be reiterate
    */
    while (!Process::isStop()) {
        LOG(LOG_INFO, " main loop start -> !!!!!!");
        if (doLoadBalance() != MONITOR_OK)
            continue;

        // After load balance. Each monitor should load the service to Config
        loadServiceToConf();

        //multiThread module
        WorkThread workThread;
        workThread.Start();

        sleep(MONITOR_SLEEP);
    }

    LOG(LOG_ERROR, "EXIT main loop!!!");
    delete p_conf;
    delete p_loadBalance;
    delete p_serviceListerner;
    return 0;
}
