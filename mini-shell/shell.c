#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include "shell.h"

#define MAX_LINE 1024
#define MAX_ARGS 64
#define DELIM " \t\r\n\a"

// Global AI context
static ai_context_t g_ai_ctx = {0};
static bool g_verbose = false;

void set_shell_verbose(bool verbose) {
    g_verbose = verbose;
}

static void report_worker_death(pid_t pid, int status) {
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        fprintf(stderr, "AI worker (PID %d) killed by signal %d (%s)\n",
                pid, sig, strsignal(sig));
    } else if (WIFEXITED(status)) {
        fprintf(stderr, "AI worker (PID %d) exited with status %d\n",
                pid, WEXITSTATUS(status));
    } else {
        fprintf(stderr, "AI worker (PID %d) terminated abnormally\n", pid);
    }
}

static void reap_and_report_worker(ai_context_t *ctx) {
    if (!ctx->is_initialized || ctx->worker_pid <= 0) return;

    int status = 0;
    pid_t pid = ctx->worker_pid;
    pid_t w = 0;

    for (int i = 0; i < 20; i++) {
        w = waitpid(pid, &status, WNOHANG);
        if (w != 0) break;
        usleep(10000);
    }

    if (w == pid) {
        report_worker_death(pid, status);
        ctx->worker_pid = 0;
    } else {
        fprintf(stderr, "AI worker (PID %d) connection lost; terminating\n", pid);
    }
}

void shell_loop() {
    char *line;
    char **args;
    int status = 1;

    signal(SIGPIPE, SIG_IGN);

    do {
        printf("mini-shell> ");
        fflush(stdout);
        line = read_line();
        if (!line) {
            printf("\n");
            break;
        }
        args = split_line(line);
        status = execute(args);

        free(line);
        free(args);
    } while (status);
    
    // Cleanup on exit
    cleanup_ai_context(&g_ai_ctx);
}

char *read_line() {
    char *line = NULL;
    size_t bufsize = 0;
    if (getline(&line, &bufsize, stdin) == -1) {
        free(line);
        return NULL;
    }
    return line;
}

char **split_line(char *line) {
    int bufsize = MAX_ARGS, position = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token;

    token = strtok(line, DELIM);
    while (token != NULL) {
        tokens[position++] = token;
        token = strtok(NULL, DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

int execute(char **args) {
    if (args[0] == NULL) return 1;

    // Check for assist command
    if (strcmp(args[0], "assist") == 0) {
        if (args[1] == NULL) {
            fprintf(stderr, "Usage: assist <query>\n");
            return 1;
        }
        
        // Concatenate all arguments after "assist"
        char prompt[AI_PROMPT_MAX] = {0};
        int offset = 0;
        for (int i = 1; args[i] != NULL && offset < AI_PROMPT_MAX - 2; i++) {
            int len = snprintf(prompt + offset, AI_PROMPT_MAX - offset, "%s ", args[i]);
            if (len > 0) offset += len;
        }
        
        // Check if existing worker already exited before sending query
        if (g_ai_ctx.is_initialized && g_ai_ctx.worker_pid > 0) {
            int wstatus = 0;
            pid_t w = waitpid(g_ai_ctx.worker_pid, &wstatus, WNOHANG);
            if (w == g_ai_ctx.worker_pid) {
                report_worker_death(g_ai_ctx.worker_pid, wstatus);
                g_ai_ctx.worker_pid = 0;
                cleanup_ai_context(&g_ai_ctx);
            }
        }

        // Initialize AI context if needed
        if (!g_ai_ctx.is_initialized) {
            if (init_ai_context(&g_ai_ctx) != 0) {
                fprintf(stderr, "AI assistant unavailable\n");
                return 1;
            }
        }
        
        return handle_assist_command(&g_ai_ctx, prompt) == 0 ? 1 : 1;
    }
    
    // Check for assist-reset command
    if (strcmp(args[0], "assist-reset") == 0) {
        cleanup_ai_context(&g_ai_ctx);
        printf("AI assistant reset\n");
        return 1;
    }

    if (strcmp(args[0], "exit") == 0) return 0;
    if (strcmp(args[0], "clear") == 0) {
        int rc = system("clear");
        (void)rc;
        return 1;
    }
    if (strcmp(args[0], "help") == 0) {
        printf("Mini Shell - Built-in Commands:\n");
        printf("  help          - Show this message\n");
        printf("  clear         - Clear the screen\n");
        printf("  assist <query> - Ask AI assistant\n");
        printf("  assist-reset  - Restart AI worker\n");
        printf("  exit          - Exit the shell\n");
        return 1;
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        if (execvp(args[0], args) == -1)
            perror("mini-shell");
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("mini-shell");
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
    return 1;
}

// AI context management
int init_ai_context(ai_context_t *ctx) {
    // Build model path
    char model_path[512];
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(model_path, sizeof(model_path), "%s/.assist_ai/model.gguf", home);
    
    // Create pipes
    if (pipe(ctx->pipe_to_worker) == -1) {
        perror("pipe to worker");
        return -1;
    }
    
    if (pipe(ctx->pipe_from_worker) == -1) {
        perror("pipe from worker");
        close(ctx->pipe_to_worker[0]);
        close(ctx->pipe_to_worker[1]);
        return -1;
    }
    
    // Fork AI worker
    ctx->worker_pid = fork();
    
    if (ctx->worker_pid == -1) {
        perror("fork");
        close(ctx->pipe_to_worker[0]);
        close(ctx->pipe_to_worker[1]);
        close(ctx->pipe_from_worker[0]);
        close(ctx->pipe_from_worker[1]);
        return -1;
    }
    
    if (ctx->worker_pid == 0) {
        // Child process: restore default SIGPIPE
        signal(SIGPIPE, SIG_DFL);

        // Redirect stdin/stdout to pipes
        dup2(ctx->pipe_to_worker[0], STDIN_FILENO);
        dup2(ctx->pipe_from_worker[1], STDOUT_FILENO);

        if (!g_verbose) {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull != -1) {
                dup2(devnull, STDERR_FILENO);
                if (devnull > STDERR_FILENO) {
                    close(devnull);
                }
            }
        }
        
        // Close unused pipe ends
        close(ctx->pipe_to_worker[0]);
        close(ctx->pipe_to_worker[1]);
        close(ctx->pipe_from_worker[0]);
        close(ctx->pipe_from_worker[1]);
        
        // Execute AI worker
        if (g_verbose) {
            execl(AI_WORKER_PATH, "ai_worker", "--verbose", model_path, NULL);
            execl(AI_WORKER_PATH, "ai_worker", "--verbose", "--echo", NULL);
        } else {
            execl(AI_WORKER_PATH, "ai_worker", model_path, NULL);
            execl(AI_WORKER_PATH, "ai_worker", "--echo", NULL);
        }
        
        // If still fails, exit
        perror("execl ai_worker");
        exit(EXIT_FAILURE);
    }
    
    // Parent: close unused pipe ends
    close(ctx->pipe_to_worker[0]);
    close(ctx->pipe_from_worker[1]);
    
    ctx->is_initialized = true;
    printf("AI assistant initialized (PID: %d)\n", ctx->worker_pid);
    
    return 0;
}

void cleanup_ai_context(ai_context_t *ctx) {
    if (!ctx->is_initialized) return;
    
    // Close pipes
    close(ctx->pipe_to_worker[1]);
    close(ctx->pipe_from_worker[0]);
    
    // Kill worker
    if (ctx->worker_pid > 0) {
        kill(ctx->worker_pid, SIGTERM);
        waitpid(ctx->worker_pid, NULL, 0);
    }
    
    ctx->is_initialized = false;
    ctx->worker_pid = 0;
}

int handle_assist_command(ai_context_t *ctx, const char *prompt) {
    char safe_prompt[AI_PROMPT_MAX];
    char response[AI_RESPONSE_MAX];
    
    // Sanitize prompt
    size_t prompt_len = strnlen(prompt, AI_PROMPT_MAX - 2);
    strncpy(safe_prompt, prompt, prompt_len);
    safe_prompt[prompt_len] = '\n';
    safe_prompt[prompt_len + 1] = '\0';
    
    // Send prompt to worker
    ssize_t written = write(ctx->pipe_to_worker[1], safe_prompt, prompt_len + 1);
    if (written == -1) {
        if (errno == EPIPE) {
            reap_and_report_worker(ctx);
        } else {
            perror("write to AI worker");
        }
        cleanup_ai_context(ctx);
        return -1;
    }
    
    // Read response in a loop until marker line, with bounded buffer and per-read timeout
    size_t total_len = 0;
    const size_t marker_len = strlen(AI_END_MARKER);
    bool marker_found = false;
    response[0] = '\0';

    while (total_len < AI_RESPONSE_MAX - 1) {
        fd_set readfds;
        struct timeval timeout = { .tv_sec = AI_TIMEOUT_SEC, .tv_usec = 0 };

        FD_ZERO(&readfds);
        FD_SET(ctx->pipe_from_worker[0], &readfds);

        int ready = select(ctx->pipe_from_worker[0] + 1, &readfds, NULL, NULL, &timeout);
        if (ready == -1) {
            if (errno == EINTR) continue;
            perror("select");
            cleanup_ai_context(ctx);
            return -1;
        } else if (ready == 0) {
            fprintf(stderr, "AI worker timeout\n");
            cleanup_ai_context(ctx);
            return -1;
        }

        ssize_t bytes_read = read(ctx->pipe_from_worker[0],
                                  response + total_len,
                                  (AI_RESPONSE_MAX - 1) - total_len);
        if (bytes_read == -1) {
            if (errno == EINTR) continue;
            perror("read from AI worker");
            reap_and_report_worker(ctx);
            cleanup_ai_context(ctx);
            return -1;
        } else if (bytes_read == 0) {
            reap_and_report_worker(ctx);
            cleanup_ai_context(ctx);
            return -1;
        }

        total_len += (size_t)bytes_read;
        response[total_len] = '\0';

        // Check if marker line is at start of buffer or preceded by newline
        if (strncmp(response, AI_END_MARKER "\n", marker_len + 1) == 0) {
            response[0] = '\0';
            marker_found = true;
            break;
        }
        char *marker_pos = strstr(response, "\n" AI_END_MARKER "\n");
        if (marker_pos != NULL) {
            *marker_pos = '\0';
            marker_found = true;
            break;
        }
    }

    if (!marker_found) {
        fprintf(stderr, "AI response exceeded buffer without end marker; resetting worker\n");
        cleanup_ai_context(ctx);
        return -1;
    }

    // Display response
    printf("AI: %s\n", response);
    fflush(stdout);
    
    return 0;
}
