#include "session/app_registry.h"
#include "session/session_protocol.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

volatile std::sig_atomic_t g_stop = 0;
pid_t g_child_pid = -1;

void signal_handler(int)
{
    g_stop = 1;
    if(g_child_pid > 0) kill(g_child_pid, SIGTERM);
}

std::string executable_directory(const char* argv0)
{
    std::string path = argv0 ? argv0 : "";
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

bool clear_cloexec(int fd)
{
    const int flags = fcntl(fd, F_GETFD);
    return flags >= 0 && fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC) == 0;
}

struct ChildResult {
    int status {127};
    bool has_message {false};
    dictpen::SessionMessage message;
};

ChildResult run_child(const std::string& bin_dir, const dictpen::AppDescriptor& app,
                      const std::string& error_message)
{
    ChildResult result;
    int sockets[2] {-1, -1};
    if(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != 0) {
        std::cerr << "SESSION error=socketpair detail=\"" << std::strerror(errno) << "\"\n";
        return result;
    }

    const pid_t child = fork();
    if(child < 0) {
        std::cerr << "SESSION error=fork detail=\"" << std::strerror(errno) << "\"\n";
        close(sockets[0]);
        close(sockets[1]);
        return result;
    }

    if(child == 0) {
        close(sockets[0]);
        if(!clear_cloexec(sockets[1])) _exit(126);
        const std::string fd_value = std::to_string(sockets[1]);
        setenv("LVGL_APP_CONTROL_FD", fd_value.c_str(), 1);
        if(error_message.empty()) unsetenv("LVGL_APP_ERROR");
        else setenv("LVGL_APP_ERROR", error_message.c_str(), 1);
        const std::string executable = bin_dir + "/" + app.executable;
        char* const arguments[] {const_cast<char*>(executable.c_str()), nullptr};
        execv(executable.c_str(), arguments);
        _exit(errno == ENOENT ? 127 : 126);
    }

    g_child_pid = child;
    close(sockets[1]);
    std::cout << "SESSION child_start app=" << app.stable_id << " pid=" << child << '\n';
    std::cout.flush();

    bool child_exited = false;
    while(!child_exited) {
        pollfd descriptor {sockets[0], POLLIN | POLLHUP, 0};
        const int polled = poll(&descriptor, 1, 50);
        if(polled > 0 && (descriptor.revents & POLLIN) != 0 && !result.has_message) {
            dictpen::SessionMessage message;
            const ssize_t received = recv(sockets[0], &message, sizeof(message), 0);
            if(received == static_cast<ssize_t>(sizeof(message)) &&
               dictpen::valid_session_message(message)) {
                result.message = message;
                result.has_message = true;
            }
            else {
                std::cerr << "SESSION warn=invalid_child_message app=" << app.stable_id << '\n';
            }
        }

        int wait_status = 0;
        const pid_t waited = waitpid(child, &wait_status, WNOHANG);
        if(waited == child) {
            child_exited = true;
            if(WIFEXITED(wait_status)) result.status = WEXITSTATUS(wait_status);
            else if(WIFSIGNALED(wait_status)) result.status = 128 + WTERMSIG(wait_status);
        }
        else if(waited < 0 && errno != EINTR) {
            child_exited = true;
        }
        if(g_stop != 0 && !child_exited) kill(child, SIGTERM);
    }

    close(sockets[0]);
    g_child_pid = -1;
    std::cout << "SESSION child_exit app=" << app.stable_id << " code=" << result.status
              << " command=" << (result.has_message ? result.message.command : 0U) << '\n';
    std::cout.flush();
    return result;
}

}  // namespace

int main(int argc, char** argv)
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGHUP, signal_handler);

    std::string bin_dir = executable_directory(argc > 0 ? argv[0] : nullptr);
    if(argc == 3 && std::string(argv[1]) == "--bin-dir") bin_dir = argv[2];
    else if(argc != 1) {
        std::cerr << "usage: lvgl_session [--bin-dir <release-bin-dir>]\n";
        return 64;
    }

    dictpen::AppId next = dictpen::AppId::launcher;
    std::string next_error;
    unsigned launcher_failures = 0;
    while(g_stop == 0) {
        const dictpen::AppDescriptor* app = dictpen::find_app(next);
        if(!app) return 65;
        const ChildResult result = run_child(bin_dir, *app, next_error);
        next_error.clear();
        if(g_stop != 0) return 143;

        if(result.has_message) {
            const auto command = static_cast<dictpen::SessionCommand>(result.message.command);
            if(command == dictpen::SessionCommand::exit_session) return 0;
            if(command == dictpen::SessionCommand::home) next = dictpen::AppId::launcher;
            else next = static_cast<dictpen::AppId>(result.message.app_id);
            launcher_failures = 0;
            continue;
        }

        if(app->id == dictpen::AppId::launcher) {
            ++launcher_failures;
            if(launcher_failures >= 3) return result.status == 0 ? 70 : result.status;
            next_error = "Launcher stopped unexpectedly";
        }
        else {
            launcher_failures = 0;
            next_error = std::string(app->display_name) + " exited with code " +
                         std::to_string(result.status);
        }
        next = dictpen::AppId::launcher;
    }
    return 143;
}
