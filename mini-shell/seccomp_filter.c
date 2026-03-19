#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <sys/syscall.h>

#define ALLOW_SYSCALL(nr) \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (nr), 0, 1), \
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)

/*
 * Install BPF seccomp filter for ai_worker after model and context are loaded.
 * When log_only is true, non-whitelisted syscalls are logged (SECCOMP_RET_LOG);
 * otherwise the process is killed (SECCOMP_RET_KILL_PROCESS).
 */
void install_seccomp_filter(bool log_only) {
    unsigned int default_action = log_only ? SECCOMP_RET_LOG : SECCOMP_RET_KILL_PROCESS;

    struct sock_filter filter[] = {
        /* Validate architecture (x86_64) */
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),

        /* Load syscall number */
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),

        /* Pipe / stdio I/O */
        ALLOW_SYSCALL(__NR_read),
        ALLOW_SYSCALL(__NR_write),
        ALLOW_SYSCALL(__NR_fstat),
        ALLOW_SYSCALL(__NR_lseek),
        ALLOW_SYSCALL(__NR_ioctl),
        ALLOW_SYSCALL(__NR_close),

        /* Memory management */
        ALLOW_SYSCALL(__NR_brk),
        ALLOW_SYSCALL(__NR_mmap),
        ALLOW_SYSCALL(__NR_munmap),
        ALLOW_SYSCALL(__NR_mprotect),
        ALLOW_SYSCALL(__NR_madvise),

        /* Threading & synchronization (llama.cpp worker threads) */
        ALLOW_SYSCALL(__NR_futex),
#ifdef __NR_clone3
        ALLOW_SYSCALL(__NR_clone3),
#endif
        ALLOW_SYSCALL(__NR_clone),
        ALLOW_SYSCALL(__NR_set_robust_list),
#ifdef __NR_rseq
        ALLOW_SYSCALL(__NR_rseq),
#endif
        ALLOW_SYSCALL(__NR_sched_yield),
        ALLOW_SYSCALL(__NR_clock_gettime),
        ALLOW_SYSCALL(__NR_nanosleep),
        ALLOW_SYSCALL(__NR_getpid),
        ALLOW_SYSCALL(__NR_gettid),

        /* Signals & clean exit */
        ALLOW_SYSCALL(__NR_rt_sigaction),
        ALLOW_SYSCALL(__NR_rt_sigprocmask),
        ALLOW_SYSCALL(__NR_rt_sigreturn),
        ALLOW_SYSCALL(__NR_exit),
        ALLOW_SYSCALL(__NR_exit_group),

        /* Default action for any other syscall (openat, execve, socket, etc.) */
        BPF_STMT(BPF_RET | BPF_K, default_action)
    };

    struct sock_fprog prog = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1) {
        perror("prctl NO_NEW_PRIVS");
        exit(1);
    }

    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) == -1) {
        perror("prctl SECCOMP");
        exit(1);
    }
}
