/********************************************************
 * Description : monitor service
 * Author      : ryan
 * Email       : ryan@rayvision.com
 * Version     : 1.0
 * Date        : 2020.08.16
 * Copyright(C): RAYVISION
 ********************************************************/

#include "log/log.h"
#include "config/xml.h"
#include "utility/log_switch.h"
#include "exception/exception.h"
#include "filesystem/file.h"
#include "filesystem/directory.h"
#include "monitor_service.h"

static bool load_configuration(Goofer::ServiceRunAccount::v_t & service_run_account, std::string & service_name, std::string & program_path, std::list<std::string> & program_param_list)
{
    service_name.c_str();
    program_path.clear();
    program_param_list.clear();

    Goofer::Xml xml;

    if (!xml.load("monitor_service.xml"))
    {
        RUN_LOG_ERR("load {%s} failed", "monitor_service.xml");
        return (false);
    }

    if (!xml.into_element("service"))
    {
        RUN_LOG_ERR("into element (%s) failed", "service");
        return (false);
    }

    std::string service_type;
    if (!xml.get_element("type", service_type))
    {
        RUN_LOG_ERR("get element (%s) failed", "type");
        return (false);
    }

    if (!xml.get_element("name", service_name))
    {
        RUN_LOG_ERR("get element (%s) failed", "name");
        return (false);
    }

    if (!xml.into_element("program"))
    {
        RUN_LOG_ERR("into element (%s) failed", "program");
        return (false);
    }

    if (!xml.get_element("path", program_path))
    {
        RUN_LOG_ERR("get element (%s) failed", "path");
        return (false);
    }

    if (!xml.get_element_block("params", "item", false, program_param_list))
    {
        RUN_LOG_ERR("get element block (%s, %s) failed", "params", "item");
        return (false);
    }

#ifdef _MSC_VER
    if ("LocalSystem" == service_type)
    {
        service_run_account = Goofer::ServiceRunAccount::local_system;
    }
    else if ("LocalService" == service_type)
    {
        service_run_account = Goofer::ServiceRunAccount::local_service;
    }
    else if ("NetworkService" == service_type)
    {
        service_run_account = Goofer::ServiceRunAccount::network_service;
    }
    else
    {
        RUN_LOG_ERR("invalid service run account");
        return (false);
    }
#else
    service_run_account = Goofer::ServiceRunAccount::local_system;
#endif // _MSC_VER

    if (service_name.empty())
    {
        RUN_LOG_ERR("service name is invalid");
        return (false);
    }

    if (program_path.empty())
    {
        RUN_LOG_ERR("program path is invalid");
        return (false);
    }

    bool program_path_is_directory = false;
    if (!Goofer::goofer_path_is_directory(program_path.c_str(), program_path_is_directory))
    {
        RUN_LOG_ERR("program path is not exist");
        return (false);
    }
    else if (program_path_is_directory)
    {
        RUN_LOG_ERR("program path is not a file");
        return (false);
    }

    program_param_list.push_front(program_path);

    return (true);
}

static void pragram_params_to_command_line(const std::list<std::string> & command_params, std::string & command_line)
{
    command_line.clear();
    for (std::list<std::string>::const_iterator iter = command_params.begin(); command_params.end() != iter; ++iter)
    {
        const std::string & param = *iter;
        if (command_params.begin() != iter)
        {
            command_line += " ";
        }
        if ((param.empty()) || ('\"' != param[0] && std::string::npos != param.find(' ')))
        {
            command_line += '\"' + param + '\"';
        }
        else
        {
            command_line += param;
        }
    }
}

MonitorService::MonitorService(Goofer::ServiceRunAccount::v_t service_run_account, const std::string & program_path, const std::list<std::string> & program_param_list)
    : SystemServiceBase(service_run_account)
    , m_running(false)
    , m_run_account(service_run_account)
    , m_program_handle(nullptr)
    , m_program_process()
    , m_program_path(program_path)
    , m_program_command_line()
    , m_program_monitor_thread()
{
    pragram_params_to_command_line(program_param_list, m_program_command_line);
}

MonitorService::~MonitorService()
{
    on_stop();
}

bool MonitorService::on_start(int argc, char * argv[])
{
    if (argc <= 0 || nullptr == argv)
    {
        return (false);
    }

    RUN_LOG_DBG("monitor service start begin");

    m_running = true;

    m_program_monitor_thread = std::thread(&MonitorService::monitor_program, this);
    if (!m_program_monitor_thread.joinable())
    {
        RUN_LOG_ERR("monitor service start failure while program monitor thread create failed");
        return (false);
    }

    RUN_LOG_DBG("monitor service start success");

    return (true);
}

bool MonitorService::on_stop()
{
    if (m_running)
    {
        RUN_LOG_DBG("monitor service stop begin");

        m_running = false;

        if (nullptr != m_program_handle)
        {
            RUN_LOG_DBG("monitor service stop while terminate program process begin");
            TerminateProcess(m_program_handle, 9);
            m_program_handle = nullptr;
            RUN_LOG_DBG("monitor service stop while terminate program process end");
        }

        if (m_program_process.running())
        {
            RUN_LOG_DBG("monitor service stop while release program process begin");
            m_program_process.release(true, 9);
            RUN_LOG_DBG("monitor service stop while release program process end");
        }

        if (m_program_monitor_thread.joinable())
        {
            RUN_LOG_DBG("monitor service stop while join program monitor thread begin");
            m_program_monitor_thread.join();
            RUN_LOG_DBG("monitor service stop while join program monitor thread end");
        }

        RUN_LOG_DBG("monitor service stop end");
    }
    return (true);
}

bool MonitorService::running()
{
    return (m_running);
}

void MonitorService::monitor_program()
{
    while (m_running)
    {
        bool run_fail = true;

#ifdef _MSC_VER
        if (Goofer::ServiceRunAccount::local_system == m_run_account)
        {
            HANDLE service_token = nullptr;
            HANDLE program_token = nullptr;

            do
            {
                HANDLE service_handle = GetCurrentProcess();
                if (!OpenProcessToken(service_handle, TOKEN_DUPLICATE, &service_token))
                {
                    RUN_LOG_ERR("open service token failure (%u)", GetLastError());
                    break;
                }

                if (!DuplicateTokenEx(service_token, MAXIMUM_ALLOWED, 0, SecurityImpersonation, TokenPrimary, &program_token))
                {
                    RUN_LOG_ERR("duplicate service token failure (%u)", GetLastError());
                    break;
                }

                DWORD session_id = WTSGetActiveConsoleSessionId();
                if (!SetTokenInformation(program_token, (TOKEN_INFORMATION_CLASS)TokenSessionId, &session_id, sizeof(session_id)))
                {
                    RUN_LOG_ERR("set program (%s) token failure (%u)", "session id", GetLastError());
                    break;
                }

                DWORD ui_control = 1;
                if (!SetTokenInformation(program_token, (TOKEN_INFORMATION_CLASS)TokenUIAccess, &ui_control, sizeof(ui_control)))
                {
                    RUN_LOG_ERR("set program (%s) token failure (%u)", "ui access", GetLastError());
                    break;
                }

                STARTUPINFO si = { sizeof(STARTUPINFO) };
                PROCESS_INFORMATION pi = { 0x0 };
                if (!CreateProcessAsUserA(program_token, m_program_path.c_str(), &m_program_command_line[0], nullptr, nullptr, TRUE, NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi))
                {
                    RUN_LOG_ERR("create process as user failure {%s} (%u)", m_program_command_line.c_str(), GetLastError());
                    break;
                }

                m_program_handle = pi.hProcess;

                RUN_LOG_DBG("create process as user success {%s} (%u)", m_program_command_line.c_str(), static_cast<uint32_t>(pi.dwProcessId));

                WaitForSingleObject(pi.hProcess, INFINITE);

                DWORD exit_code = 0;
                GetExitCodeProcess(pi.hProcess, &exit_code);

                RUN_LOG_DBG("get process exit code (%d)", static_cast<int>(exit_code));

                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);

                m_program_handle = nullptr;

                run_fail = false;
            } while (false);

            if (nullptr != service_token)
            {
                CloseHandle(service_token);
            }

            if (nullptr != program_token)
            {
                CloseHandle(program_token);
            }
        }
        else
#endif // _MSC_VER
        {
            m_program_process.set_process_args(m_program_command_line);
            if (m_program_process.acquire())
            {
                RUN_LOG_DBG("create process success {%s} (%u)", m_program_command_line.c_str(), static_cast<uint32_t>(m_program_process.process_id()));

                int exit_code = 0;
                m_program_process.wait_exit(exit_code);

                RUN_LOG_DBG("get process exit code (%d)", exit_code);

                m_program_process.release(true, 0);

                run_fail = false;
            }
            else
            {
                RUN_LOG_ERR("create process failure {%s}", m_program_command_line.c_str());
            }
        }

        if (run_fail)
        {
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

int main(int argc, char * argv[])
{
    std::string pathname;
    Goofer::goofer_get_current_process_pathname(pathname);

    std::string dirname;
    std::string filename;
    Goofer::goofer_extract_path(pathname.c_str(), dirname, filename, true);

    Goofer::goofer_set_current_work_directory(dirname);

    Goofer::goofer_set_dump_directory(".\\dump", filename.substr(0, filename.rfind('.')).c_str());

    Goofer::Singleton<Goofer::LogSwitch>::instance().init("log.ini");

    Goofer::ServiceRunAccount::v_t service_run_account = Goofer::ServiceRunAccount::local_system;
    std::string service_name;
    std::string program_path;
    std::list<std::string> program_param_list;
    if (!load_configuration(service_run_account, service_name, program_path, program_param_list))
    {
        RUN_LOG_ERR("load configuration failure");
        return (-1);
    }

    MonitorService monitor_service(service_run_account, program_path, program_param_list);
    if (!monitor_service.run(service_name.c_str(), argc, argv))
    {
        RUN_LOG_ERR("monitor service run failure");
        return (-2);
    }

    Goofer::Singleton<Goofer::LogSwitch>::instance().exit();

    return (0);
}
