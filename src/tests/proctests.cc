#include <cassert>
#include <iostream>
#include <list>
#include <utility>
#include <string>

#include "service.h"
#include "proc-service.h"

// Tests of process-service related functionality.
//
// These tests work mostly by completely mocking out the base_process_service class. The mock
// implementations can be found in test-baseproc.cc.

extern eventloop_t event_loop;

constexpr static auto REG = dependency_type::REGULAR;
constexpr static auto WAITS = dependency_type::WAITS_FOR;

// Friend interface to access base_process_service private/protected members.
class base_process_service_test
{
    public:
    static void exec_succeeded(base_process_service *bsp)
    {
        bsp->waiting_for_execstat = false;
        bsp->exec_succeeded();
    }

    static void exec_failed(base_process_service *bsp, int errcode)
    {
        run_proc_err err;
        err.stage = exec_stage::DO_EXEC;
        err.st_errno = errcode;
    	bsp->waiting_for_execstat = false;
    	bsp->exec_failed(err);
    }

    static void handle_exit(base_process_service *bsp, int exit_status)
    {
        bsp->pid = -1;
        bsp->handle_exit_status(bp_sys::exit_status(true, false, exit_status));
    }

    static void handle_signal_exit(base_process_service *bsp, int signo)
    {
        bsp->pid = -1;
        bsp->handle_exit_status(bp_sys::exit_status(false, true, signo));
    }

    static int get_notification_fd(base_process_service *bsp)
    {
        return bsp->notification_fd;
    }
};

namespace bp_sys {
    // last signal sent:
    extern int last_sig_sent;
    extern pid_t last_forked_pid;
}

static void init_service_defaults(base_process_service &ps)
{
    ps.set_restart_interval(time_val(10,0), 3);
    ps.set_restart_delay(time_val(0, 200000000)); // 200 milliseconds
    ps.set_stop_timeout(time_val(10,0));
}

// Regular service start
void test_proc_service_start()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    sset.add_service(&p);

    p.start(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);
    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
}

// Test start with readiness notification
void test_proc_notify_start()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    p.set_notification_fd(3);
    sset.add_service(&p);

    p.start(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    int nfd = base_process_service_test::get_notification_fd(&p);
    assert(nfd > 0);

    char notifystr[] = "ok started\n";
    std::vector<char> rnotifystr;
    rnotifystr.insert(rnotifystr.end(), notifystr, notifystr + sizeof(notifystr));
    bp_sys::supply_read_data(nfd, std::move(rnotifystr));

    event_loop.regd_fd_watchers[nfd]->fd_event(event_loop, nfd, dasynq::IN_EVENTS);

    assert(p.get_state() == service_state_t::STARTED);
    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
}

// Unexpected termination
void test_proc_unexpected_term()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    sset.add_service(&p);

    p.start(true);
    sset.process_queues();

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);

    base_process_service_test::handle_exit(&p, 0);

    assert(p.get_state() == service_state_t::STOPPED);
    assert(p.get_stop_reason() == stopped_reason_t::TERMINATED);
    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
}

// Unexpected termination with restart
void test_proc_term_restart()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    p.set_auto_restart(true);
    sset.add_service(&p);

    p.start(true);
    sset.process_queues();

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);
    assert(event_loop.active_timers.size() == 0);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    // Starting, restart timer should be armed:
    assert(p.get_state() == service_state_t::STARTING);
    assert(event_loop.active_timers.size() == 1);

    event_loop.advance_time(time_val(0, 200000000));
    assert(event_loop.active_timers.size() == 0);

    sset.process_queues();
    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);
    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
}

void test_proc_term_restart2()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    service_record b {&sset, "boot"};
    sset.add_service(&b);

    process_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    p.set_auto_restart(true);
    sset.add_service(&p);

    b.add_dep(&p, WAITS);

    b.start(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);
    assert(event_loop.active_timers.size() == 0);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    // Starting, restart timer should be armed:
    assert(p.get_state() == service_state_t::STARTING);
    assert(event_loop.active_timers.size() == 1);

    event_loop.advance_time(time_val(0, 200000000));
    assert(event_loop.active_timers.size() == 0);

    sset.process_queues();
    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);
    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
    sset.remove_service(&b);
}


// Termination via stop request
void test_term_via_stop()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    sset.add_service(&p);

    p.start(true);
    sset.process_queues();

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);
    assert(event_loop.active_timers.size() == 0);

    p.stop(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPING);
    assert(event_loop.active_timers.size() == 1);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPED);
    assert(p.get_stop_reason() == stopped_reason_t::NORMAL);
    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
}

// Termination via stop request, ensure reason is reset:
void test_term_via_stop2()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    sset.add_service(&p);

    p.start(true);
    sset.process_queues();

    // first set it up with failure reason:

    base_process_service_test::exec_failed(&p, ENOENT);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPED);
    assert(p.get_stop_reason() == stopped_reason_t::EXECFAILED);

    // now restart clean:

    p.start(true);
    sset.process_queues();

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);
    assert(event_loop.active_timers.size() == 0);

    p.stop(true);
    sset.process_queues();
    assert(p.get_state() == service_state_t::STOPPING);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();
    assert(p.get_state() == service_state_t::STOPPED);
    assert(p.get_stop_reason() == stopped_reason_t::NORMAL);
    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
}

// Time-out during start
void test_proc_start_timeout()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    scripted_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    p.set_start_timeout(time_val(10,0));
    sset.add_service(&p);

    p.start(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    event_loop.advance_time(time_val(10,0));
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPING);

    base_process_service_test::handle_signal_exit(&p, SIGTERM);
    sset.process_queues();

    // We set no stop script, so state should now be STOPPED with no timer set
    assert(p.get_state() == service_state_t::STOPPED);
    assert(p.get_stop_reason() == stopped_reason_t::TIMEDOUT);
    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
}

// Test that a timeout doesn't stop a "waits for" dependent to fail to start
void test_proc_start_timeout2()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    scripted_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    p.set_start_timeout(time_val {1,0});
    init_service_defaults(p);
    sset.add_service(&p);

    service_record ts {&sset, "test-service-1", service_type_t::INTERNAL,
        {{&p, dependency_type::WAITS_FOR}} };

    ts.start(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);
    assert(ts.get_state() == service_state_t::STARTING);

    event_loop.advance_time(time_val {1,0}); // start timer should expire
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPING);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPED);
    assert(p.get_stop_reason() == stopped_reason_t::TIMEDOUT);
    assert(ts.get_state() == service_state_t::STARTED);
    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
}

// Test exec() failure for process service start.
void test_proc_start_execfail()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    sset.add_service(&p);

    p.start(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::exec_failed(&p, ENOENT);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPED);
    assert(p.get_stop_reason() == stopped_reason_t::EXECFAILED);
    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
}

// Test no ready notification before process terminates
void test_proc_notify_fail()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    p.set_notification_fd(3);
    sset.add_service(&p);

    p.start(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    int nfd = base_process_service_test::get_notification_fd(&p);
    assert(nfd > 0);

    // Signal EOF on notify fd:
    event_loop.regd_fd_watchers[nfd]->fd_event(event_loop, nfd, dasynq::IN_EVENTS);

    assert(p.get_state() == service_state_t::STOPPING);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPED);
    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
}

// Test stop timeout
void test_proc_stop_timeout()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    p.set_stop_timeout(time_val {10, 0});
    sset.add_service(&p);

    p.start(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);

    p.stop(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPING);
    assert(bp_sys::last_sig_sent == SIGTERM);

    event_loop.advance_time(time_val {10, 0}); // expire stop timer
    sset.process_queues();

    // kill signal (SIGKILL) should have been sent; process not dead until it's dead, however
    assert(p.get_state() == service_state_t::STOPPING);
    assert(bp_sys::last_sig_sent == SIGKILL);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPED);
    assert(p.get_stop_reason() == stopped_reason_t::NORMAL);

    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
}

// Smooth recovery
void test_proc_smooth_recovery1()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    p.set_smooth_recovery(true);
    p.set_restart_delay(time_val {0, 1000});
    sset.add_service(&p);

    p.start(true);
    sset.process_queues();

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    pid_t first_instance = bp_sys::last_forked_pid;

    assert(p.get_state() == service_state_t::STARTED);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    // since time hasn't been changed, we expect that the process has not yet been re-launched:
    assert(first_instance == bp_sys::last_forked_pid);
    assert(p.get_state() == service_state_t::STARTED);

    event_loop.advance_time(time_val {0, 1000});
    sset.process_queues();

    // Now a new process should've been launched:
    assert(first_instance + 1 == bp_sys::last_forked_pid);
    assert(p.get_state() == service_state_t::STARTED);

    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
}

// Smooth recovery without restart delay
void test_proc_smooth_recovery2()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    p.set_smooth_recovery(true);
    p.set_restart_delay(time_val(0, 0));
    sset.add_service(&p);

    p.start(true);
    sset.process_queues();

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    pid_t first_instance = bp_sys::last_forked_pid;

    assert(p.get_state() == service_state_t::STARTED);
    assert(event_loop.active_timers.size() == 0);

    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    // no restart delay, process should restart immediately:
    assert(first_instance + 1 == bp_sys::last_forked_pid);
    assert(p.get_state() == service_state_t::STARTED);
    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
}

// Test stop timeout
void test_scripted_stop_timeout()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    string stopcommand = "stop-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    scripted_service p {&sset, "testscripted", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    p.set_stop_command(stopcommand, command_offsets);
    p.set_stop_timeout(time_val {10, 0});
    sset.add_service(&p);

    p.start(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();
    base_process_service_test::handle_exit(&p, 0);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);

    p.stop(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPING);

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    // should still be stopping:
    assert(p.get_state() == service_state_t::STOPPING);

    event_loop.advance_time(time_val {10, 0}); // expire stop timer
    sset.process_queues();

    // kill signal (SIGKILL) should have been sent; process not dead until it's dead, however
    assert(p.get_state() == service_state_t::STOPPING);
    assert(bp_sys::last_sig_sent == SIGKILL);

    base_process_service_test::handle_exit(&p, SIGKILL);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPED);
    assert(p.get_stop_reason() == stopped_reason_t::NORMAL);

    assert(event_loop.active_timers.size() == 0);

    sset.remove_service(&p);
}

void test_scripted_start_fail()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    string stopcommand = "stop-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    scripted_service p {&sset, "testscripted", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    p.set_stop_command(stopcommand, command_offsets);
    sset.add_service(&p);

    service_record *s2 = new service_record(&sset, "test-service-2", service_type_t::INTERNAL, {{&p, REG}});
    service_record *s3 = new service_record(&sset, "test-service-3",
            service_type_t::INTERNAL, {{&p, REG}, {s2, REG}});
    sset.add_service(s2);
    sset.add_service(s3);

    s3->start(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();
    base_process_service_test::handle_exit(&p, 0x1);  // exit fail
    sset.process_queues();

    // failed to start:
    assert(p.get_state() == service_state_t::STOPPED);
    assert(s2->get_state() == service_state_t::STOPPED);
    assert(s3->get_state() == service_state_t::STOPPED);
    assert(p.get_stop_reason() == stopped_reason_t::FAILED);
    assert(s2->get_stop_reason() == stopped_reason_t::DEPFAILED);
    assert(s3->get_stop_reason() == stopped_reason_t::DEPFAILED);

    event_loop.active_timers.clear();
    sset.remove_service(&p);

    assert(sset.count_active_services() == 0);
}

void test_scripted_stop_fail()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    string stopcommand = "stop-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    scripted_service p {&sset, "testscripted", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    p.set_stop_command(stopcommand, command_offsets);
    sset.add_service(&p);

    service_record *s2 = new service_record(&sset, "test-service-2", service_type_t::INTERNAL, {});
    service_record *s3 = new service_record(&sset, "test-service-3", service_type_t::INTERNAL,
            {{s2, REG}, {&p, REG}});
    service_record *s4 = new service_record(&sset, "test-service-4", service_type_t::INTERNAL,
            {{&p, REG}, {s3, REG}});
    sset.add_service(s2);
    sset.add_service(s3);
    sset.add_service(s4);

    s4->start(true);
    sset.process_queues();

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();
    base_process_service_test::handle_exit(&p, 0x0);  // success
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);
    assert(s2->get_state() == service_state_t::STARTED);
    assert(s3->get_state() == service_state_t::STARTED);
    assert(s4->get_state() == service_state_t::STARTED);

    pid_t last_forked = bp_sys::last_forked_pid;

    s4->stop(true);
    sset.process_queues();

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();
    base_process_service_test::handle_exit(&p, 0x1);  // failure
    sset.process_queues();

    // The stop command should be executed once:
    assert((bp_sys::last_forked_pid - last_forked) == 1);

    assert(p.get_state() == service_state_t::STOPPED);
    assert(s2->get_state() == service_state_t::STOPPED);
    assert(s3->get_state() == service_state_t::STOPPED);
    assert(s4->get_state() == service_state_t::STOPPED);

    event_loop.active_timers.clear();
    sset.remove_service(&p);
}

void test_scripted_start_skip()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    scripted_service p {&sset, "testscripted", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    service_flags_t sflags;
    sflags.skippable = true;
    p.set_flags(sflags);
    sset.add_service(&p);

    service_record *s2 = new service_record(&sset, "test-service-2", service_type_t::INTERNAL, {{&p, REG}});
    sset.add_service(s2);

    s2->start(true);
    sset.process_queues();
    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();
    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::handle_signal_exit(&p, SIGINT); // interrupted
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);
    assert(s2->get_state() == service_state_t::STARTED);
    assert(p.was_start_skipped());
    assert(! s2->was_start_skipped());
    assert(sset.count_active_services() == 2);

    s2->stop(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPED);
    assert(s2->get_state() == service_state_t::STOPPED);
    assert(p.get_stop_reason() == stopped_reason_t::NORMAL);
    assert(s2->get_stop_reason() == stopped_reason_t::NORMAL);
    assert(sset.count_active_services() == 0);

    event_loop.active_timers.clear();
    sset.remove_service(&p);
}

// Test interrupting start of a service marked skippable
void test_scripted_start_skip2()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    scripted_service p {&sset, "testscripted", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    service_flags_t sflags;
    sflags.skippable = true;
    sflags.start_interruptible = true;
    p.set_flags(sflags);
    sset.add_service(&p);

    service_record *s2 = new service_record(&sset, "test-service-2", service_type_t::INTERNAL, {{&p, REG}});
    sset.add_service(s2);

    s2->start(true);
    sset.process_queues();
    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();
    assert(p.get_state() == service_state_t::STARTING);

    s2->stop(true);  // abort startup; p should be cancelled
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPING);

    base_process_service_test::handle_signal_exit(&p, SIGINT); // interrupted
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPED);
    assert(s2->get_state() == service_state_t::STOPPED);
    assert(p.get_stop_reason() == stopped_reason_t::NORMAL);
    assert(s2->get_stop_reason() == stopped_reason_t::NORMAL);
    assert(sset.count_active_services() == 0);

    event_loop.active_timers.clear();
    sset.remove_service(&p);
}

// Test that starting a service with a waits-for dependency on another - currently stopping - service,
// causes that service to re-start.
void test_waitsfor_restart()
{
    using namespace std;

    service_set sset;

    string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p {&sset, "testproc", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    sset.add_service(&p);

    service_record tp {&sset, "test-service", service_type_t::INTERNAL, {{&p, WAITS}}};
    sset.add_service(&tp);

    // start p:

    p.start(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STARTED);
    assert(event_loop.active_timers.size() == 0);

    // begin stopping p:

    p.stop(true);
    sset.process_queues();

    assert(p.get_state() == service_state_t::STOPPING);

    // start tp (which waits-for p):

    tp.start(true);
    sset.process_queues();

    assert(tp.get_state() == service_state_t::STARTING);
    assert(p.get_state() == service_state_t::STOPPING);

    // p terminates (finishes stopping). Then it should re-start...
    base_process_service_test::handle_signal_exit(&p, SIGTERM);
    sset.process_queues();

    assert(tp.get_state() == service_state_t::STARTING);
    assert(p.get_state() == service_state_t::STARTING);

    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    assert(tp.get_state() == service_state_t::STARTED);
    assert(p.get_state() == service_state_t::STARTED);

    sset.remove_service(&tp);
    sset.remove_service(&p);
}


#define RUN_TEST(name, spacing) \
    std::cout << #name "..." spacing << std::flush; \
    name(); \
    std::cout << "PASSED" << std::endl;

int main(int argc, char **argv)
{
    RUN_TEST(test_proc_service_start, "   ");
    RUN_TEST(test_proc_notify_start, "    ");
    RUN_TEST(test_proc_unexpected_term, " ");
    RUN_TEST(test_proc_term_restart, "    ");
    RUN_TEST(test_proc_term_restart2, "   ");
    RUN_TEST(test_term_via_stop, "        ");
    RUN_TEST(test_term_via_stop2, "       ");
    RUN_TEST(test_proc_start_timeout, "   ");
    RUN_TEST(test_proc_start_timeout2, "  ");
    RUN_TEST(test_proc_start_execfail, "  ");
    RUN_TEST(test_proc_notify_fail, "     ");
    RUN_TEST(test_proc_stop_timeout, "    ");
    RUN_TEST(test_proc_smooth_recovery1, "");
    RUN_TEST(test_proc_smooth_recovery2, "");
    RUN_TEST(test_scripted_stop_timeout, "");
    RUN_TEST(test_scripted_start_fail, "  ");
    RUN_TEST(test_scripted_stop_fail, "   ");
    RUN_TEST(test_scripted_start_skip, "  ");
    RUN_TEST(test_scripted_start_skip2, " ");
    RUN_TEST(test_waitsfor_restart, "     ");
}
