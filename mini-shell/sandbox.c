#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/capability.h>
#include <errno.h>

#ifdef HAVE_LIBCAP
#include <sys/capability.h>
#endif

extern void install_seccomp_filter(void);

static void log_proc_cap_status(void) {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) {
        perror("fopen /proc/self/status");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "CapEff:", 7) == 0 ||
            strncmp(line, "CapPrm:", 7) == 0 ||
            strncmp(line, "NoNewPrivs:", 11) == 0) {
            fprintf(stderr, "[sandbox] %s", line);
        }
    }
    fclose(f);
}

static void drop_capabilities(void) {
#ifdef HAVE_LIBCAP
    cap_t caps = cap_init();
    if (caps) {
        if (cap_set_proc(caps) == -1) {
            perror("cap_set_proc");
        }
        cap_free(caps);
    }
#else
    struct __user_cap_header_struct hdr = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0
    };
    struct __user_cap_data_struct data[2];
    memset(data, 0, sizeof(data));
    if (syscall(SYS_capset, &hdr, data) == -1) {
        perror("capset");
    }
#endif
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1) {
        perror("prctl PR_SET_NO_NEW_PRIVS");
    }
}

void apply_sandbox(void) {
    // 1. Attempt namespace isolation (requires CAP_SYS_ADMIN / unprivileged userns)
    if (unshare(CLONE_NEWNS | CLONE_NEWPID) == -1) {
        fprintf(stderr, "Warning: namespace isolation unavailable (%s)\n", strerror(errno));
    }

    // 2. Set resource limits
    struct rlimit mem_limit = {
        .rlim_cur = 2UL * 1024 * 1024 * 1024,
        .rlim_max = 2UL * 1024 * 1024 * 1024
    };
    if (setrlimit(RLIMIT_AS, &mem_limit) == -1) {
        perror("setrlimit RLIMIT_AS");
    }

    struct rlimit cpu_limit = {
        .rlim_cur = 600,
        .rlim_max = 600
    };
    if (setrlimit(RLIMIT_CPU, &cpu_limit) == -1) {
        perror("setrlimit RLIMIT_CPU");
    }

    // 3. Disable core dumps
    struct rlimit core_limit = { .rlim_cur = 0, .rlim_max = 0 };
    if (setrlimit(RLIMIT_CORE, &core_limit) == -1) {
        perror("setrlimit RLIMIT_CORE");
    }

    // 4. Close inherited file descriptors beyond stdin/stdout/stderr
    for (int fd = 3; fd < 256; fd++) {
        close(fd);
    }

    // 5. Drop all Linux capabilities and prevent privilege escalation
    drop_capabilities();
    log_proc_cap_status();

    // 6. Seccomp currently disabled by default (see Step 8 / ASSIST_SECCOMP)
    fprintf(stderr, "Sandbox applied (seccomp not enabled)\n");
}
