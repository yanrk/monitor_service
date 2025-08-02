/********************************************************
 * Description : monitor service
 * Author      : yanrk
 * Email       : yanrkchina@163.com
 * Version     : 1.0
 * Date        : 2020.08.16
 * Copyright(C): 2025
 ********************************************************/

#ifndef MONITOR_SERVICE_H
#define MONITOR_SERVICE_H


#include <list>
#include <vector>
#include <string>
#include <thread>
#include "process/process.h"
#include "filesystem/service.h"

class MonitorService : public Goofer::SystemServiceBase
{
public:
    MonitorService(Goofer::ServiceRunAccount::v_t service_run_account, const std::string & program_path, const std::list<std::string> & program_param_list);
    virtual ~MonitorService();

public:
    virtual bool on_start(int argc, char * argv[]) override;
    virtual bool on_stop() override;
    virtual bool running() override;

private:
    void monitor_program();

private:
    bool                                m_running;
    Goofer::ServiceRunAccount::v_t      m_run_account;
    HANDLE                              m_program_handle;
    Goofer::Process                     m_program_process;
    std::string                         m_program_path;
    std::string                         m_program_command_line;
    std::thread                         m_program_monitor_thread;
};


#endif // MONITOR_SERVICE_H
