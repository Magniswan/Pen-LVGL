#include "runtime/runtime_sandbox.h"

#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <drm.h>
#include <drm_mode.h>
#endif

namespace dictpen {
namespace {

bool parse_integer_env(const char* name, int& result)
{
    const char* value = std::getenv(name);
    if(value == nullptr || *value == '\0') return false;
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    if(errno != 0 || end == value || *end != '\0' || parsed < 0 || parsed > INT_MAX) {
        return false;
    }
    result = static_cast<int>(parsed);
    return true;
}

#if defined(__linux__) && defined(__aarch64__)

constexpr std::uint32_t kKill = SECCOMP_RET_KILL_PROCESS;
constexpr std::uint32_t kAllow = SECCOMP_RET_ALLOW;

void append_statement(std::vector<sock_filter>& filter, std::uint16_t code,
                      std::uint32_t value)
{
    filter.push_back(sock_filter {code, 0, 0, value});
}

void append_jump(std::vector<sock_filter>& filter, std::uint16_t code,
                 std::uint32_t value, std::uint8_t on_true, std::uint8_t on_false)
{
    filter.push_back(sock_filter {code, on_true, on_false, value});
}

void append_allowed_syscall(std::vector<sock_filter>& filter, int syscall_number)
{
    append_jump(filter, BPF_JMP | BPF_JEQ | BPF_K,
                static_cast<std::uint32_t>(syscall_number), 0, 1);
    append_statement(filter, BPF_RET | BPF_K, kAllow);
}

void append_flag_restricted_syscall(std::vector<sock_filter>& filter, int syscall_number,
                                    std::size_t argument_offset, std::uint32_t forbidden)
{
    append_jump(filter, BPF_JMP | BPF_JEQ | BPF_K,
                static_cast<std::uint32_t>(syscall_number), 0, 5);
    append_statement(filter, BPF_LD | BPF_W | BPF_ABS,
                     static_cast<std::uint32_t>(argument_offset));
    append_statement(filter, BPF_ALU | BPF_AND | BPF_K, forbidden);
    append_jump(filter, BPF_JMP | BPF_JEQ | BPF_K, 0, 0, 1);
    append_statement(filter, BPF_RET | BPF_K, kAllow);
    append_statement(filter, BPF_RET | BPF_K, kKill);
}

bool append_drm_ioctl_filter(std::vector<sock_filter>& filter, int drm_fd)
{
    constexpr std::uint32_t requests[] {
        DRM_IOCTL_MODE_SETPLANE,
        DRM_IOCTL_MODE_RMFB,
        DRM_IOCTL_MODE_DESTROY_DUMB,
    };
    constexpr std::size_t request_count = sizeof(requests) / sizeof(requests[0]);
    constexpr std::size_t block_instructions = 4 + request_count * 2;
    static_assert(block_instructions <= std::numeric_limits<std::uint8_t>::max());

    append_jump(filter, BPF_JMP | BPF_JEQ | BPF_K, __NR_ioctl, 0,
                static_cast<std::uint8_t>(block_instructions));
    append_statement(filter, BPF_LD | BPF_W | BPF_ABS,
                     offsetof(seccomp_data, args[0]));
    append_jump(filter, BPF_JMP | BPF_JEQ | BPF_K,
                static_cast<std::uint32_t>(drm_fd), 0,
                static_cast<std::uint8_t>(1 + request_count * 2));
    append_statement(filter, BPF_LD | BPF_W | BPF_ABS,
                     offsetof(seccomp_data, args[1]));
    for(const std::uint32_t request : requests) {
        append_jump(filter, BPF_JMP | BPF_JEQ | BPF_K, request, 0, 1);
        append_statement(filter, BPF_RET | BPF_K, kAllow);
    }
    append_statement(filter, BPF_RET | BPF_K, kKill);
    return true;
}

bool install_aarch64_filter(int drm_fd, std::string& error)
{
    std::vector<sock_filter> filter;
    filter.reserve(180);

    append_statement(filter, BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch));
    append_jump(filter, BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0);
    append_statement(filter, BPF_RET | BPF_K, kKill);
    append_statement(filter, BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr));

    constexpr std::uint32_t write_flags =
        O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND | O_EXCL
#ifdef O_TMPFILE
        | O_TMPFILE
#endif
        ;
    append_flag_restricted_syscall(filter, __NR_openat,
                                   offsetof(seccomp_data, args[2]), write_flags);
    append_flag_restricted_syscall(filter, __NR_mmap,
                                   offsetof(seccomp_data, args[2]), PROT_EXEC);
    append_flag_restricted_syscall(filter, __NR_mprotect,
                                   offsetof(seccomp_data, args[2]), PROT_EXEC);
    if(!append_drm_ioctl_filter(filter, drm_fd)) {
        error = "could not construct DRM ioctl policy";
        return false;
    }

    const int allowed_syscalls[] {
        __NR_read, __NR_write, __NR_readv, __NR_writev, __NR_close,
        __NR_lseek, __NR_pread64, __NR_fstat, __NR_newfstatat, __NR_statx,
        __NR_faccessat, __NR_readlinkat, __NR_getcwd,
        __NR_munmap, __NR_mremap, __NR_madvise, __NR_brk,
        __NR_rt_sigaction, __NR_rt_sigprocmask, __NR_rt_sigreturn,
        __NR_sigaltstack, __NR_ppoll, __NR_pselect6,
        __NR_clock_gettime, __NR_clock_nanosleep, __NR_nanosleep,
        __NR_futex, __NR_set_tid_address, __NR_set_robust_list, __NR_rseq,
        __NR_prlimit64, __NR_getrandom, __NR_getpid, __NR_getppid,
        __NR_gettid, __NR_getuid, __NR_geteuid, __NR_getgid, __NR_getegid,
        __NR_uname, __NR_sysinfo, __NR_getrusage, __NR_times,
        __NR_sched_yield, __NR_sched_getaffinity,
        __NR_fcntl, __NR_dup, __NR_dup3,
        __NR_recvfrom, __NR_sendto, __NR_recvmsg, __NR_sendmsg,
        __NR_shutdown, __NR_getsockopt,
        __NR_fstatfs, __NR_statfs,
        __NR_eventfd2, __NR_epoll_create1, __NR_epoll_ctl, __NR_epoll_pwait,
        __NR_restart_syscall, __NR_exit, __NR_exit_group,
    };
    for(const int syscall_number : allowed_syscalls) {
        append_allowed_syscall(filter, syscall_number);
    }
    append_statement(filter, BPF_RET | BPF_K, kKill);

    if(filter.size() > std::numeric_limits<unsigned short>::max()) {
        error = "seccomp filter is too large";
        return false;
    }
    sock_fprog program {
        static_cast<unsigned short>(filter.size()),
        filter.data(),
    };
    if(::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        error = std::string("PR_SET_NO_NEW_PRIVS failed: ") + std::strerror(errno);
        return false;
    }
    if(::prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0) {
        error = std::string("PR_SET_SECCOMP failed: ") + std::strerror(errno);
        return false;
    }
    return true;
}

#endif

}  // namespace

bool install_runtime_sandbox(std::string& error)
{
    const char* marker = std::getenv("LVGL_SANDBOX_REQUIRED");
    if(marker == nullptr) return true;
    if(std::strcmp(marker, "0") == 0) return true;
    if(std::strcmp(marker, "1") != 0) {
        error = "invalid LVGL_SANDBOX_REQUIRED marker";
        return false;
    }

#if defined(__linux__) && defined(__aarch64__)
    int drm_fd = -1;
    int expected_uid = -1;
    if(!parse_integer_env("LVGL_DRM_FD", drm_fd) || drm_fd < 3 ||
       !parse_integer_env("LVGL_APP_UID", expected_uid) || expected_uid < 1) {
        error = "missing sandbox descriptor or application identity";
        return false;
    }
    if(::geteuid() == 0 || ::geteuid() != static_cast<uid_t>(expected_uid) ||
       ::getegid() != static_cast<gid_t>(expected_uid)) {
        error = "sessiond did not apply the independent application identity";
        return false;
    }
    if(::prctl(PR_GET_DUMPABLE, 0, 0, 0, 0) != 0) {
        error = "application process remains dumpable";
        return false;
    }
    return install_aarch64_filter(drm_fd, error);
#else
    error = "mandatory runtime sandbox is unavailable on this architecture";
    return false;
#endif
}

}  // namespace dictpen
