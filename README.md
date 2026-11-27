# Offline AI Assistant Shell (`myshell`)

> An offline AI assistant integrated into a custom C shell (`myshell`), delegating local LLM inference to a separate worker process (`ai_worker`) over Unix pipes using [`llama.cpp`](https://github.com/ggml-org/llama.cpp).

---

## Overview

The **Offline AI Assistant Shell** separates interactive shell execution from local LLM inference across two processes:

- **`myshell` (`mini-shell/main.c`, `mini-shell/shell.c`, `mini-shell/shell.h`)**: Interactive shell loop that runs standard commands via `fork()`/`execvp()`, spawns `ai_worker` via `fork()`/`execl()` (located next to `myshell` via `/proc/self/exe`), communicates over unidirectional pipes, reads replies until the `__ASSIST_END__` marker with bounded buffers and per-read `select()` timeouts, reports worker crashes/signals via `waitpid()` (`WIFSIGNALED`/`WTERMSIG`), and respawns the worker on the next `assist` query.
- **`ai_worker` (`mini-shell/ai_worker.c`, `mini-shell/sandbox.c`, `mini-shell/seccomp_filter.c`)**: Runs TinyLlama-1.1B-Chat locally on CPU via `llama.cpp` (or falls back to `--echo` mode if no accessible GGUF model is present), formats queries with the TinyLlama chat template (`<|system|>...<|user|>...<|assistant|>`), clears KV cache memory between queries (`llama_memory_clear(llama_get_memory(ctx), true)`), silences `llama.cpp` logs unless `--verbose` is given, and applies OS-level process restrictions before loading the model.

---

## Architecture

```text
+-----------------------------------+   pipe_to_worker    +------------------------------------+
| myshell (Parent / Trusted Shell)  | ------------------> | ai_worker (Child / Worker Process) |
| - Command parsing & job execution |                     | - setrlimit() (AS, CPU, CORE)      |
| - Bounded timeout pipe reads      | <------------------ | - capset() + PR_SET_NO_NEW_PRIVS   |
| - Respawns worker & reports exits |  pipe_from_worker   | - llama.cpp inference / echo mode  |
+-----------------------------------+                     +------------------------------------+
```

---

## Security & Isolation Status

| Mechanism | Status | Details & Verification |
|---|---|---|
| `rlimits` | **Working** | `sandbox.c` sets `RLIMIT_AS` (2 GiB), `RLIMIT_CPU` (600 s cumulative CPU time), and `RLIMIT_CORE` (0 bytes, disables core dumps) via `setrlimit()`. |
| `capability dropping` | **Working** | `sandbox.c` clears effective/permitted/inheritable capabilities via `capset()` (or `libcap` when `HAVE_LIBCAP` is defined) and sets `PR_SET_NO_NEW_PRIVS = 1`. Verified via `/proc/self/status` inside `ai_worker`: `CapEff: 0000000000000000`, `CapPrm: 0000000000000000`, `NoNewPrivs: 1`. |
| `seccomp` | **Working** | `seccomp_filter.c` installs a BPF `SECCOMP_RET_KILL_PROCESS` syscall whitelist (`install_seccomp_filter`) after `llama_init_from_model()` (and before `echo_worker()`), allowing `strace -f`-audited runtime/threading syscalls (`read`, `write`, `mmap`, `munmap`, `mprotect`, `madvise`, `brk`, `futex`, `clone3`, `clone`, `set_robust_list`, `rseq`, `rt_sig*`, `exit*`, etc.) and blocking `openat`, `execve`, `socket`, etc. Verified `Seccomp: 2` and 10/10 queries in kill mode. |
| `namespaces` | **Not working** | `unshare(CLONE_NEWNS | CLONE_NEWPID)` fails with `EPERM` (`Operation not permitted`) when run as a normal unprivileged user without `CAP_SYS_ADMIN` or user-namespace setup. |
| `model integrity check` | **Partial** | `mini-shell/verify_model.sh` manually checks/writes `~/.assist_ai/model.md5` with `md5sum`, but neither `myshell` nor `ai_worker` checks model hashes at startup, and MD5 is non-cryptographic. |

---

## Threat Model

- **What the worker (`ai_worker`) can do:**
  - During startup (before post-load seccomp), read `~/.assist_ai/model.gguf`, load shared libraries, and read `/proc/self/status` as the invoking user.
  - After model and context load, read prompts from `stdin` (pipe from `myshell`), run multi-threaded CPU inference (`clone3`/`futex`/`mmap`), and write output to `stdout` (pipe to `myshell`).
  - Allocate up to 2 GiB of virtual address space (`RLIMIT_AS`) and consume up to 600 s of cumulative CPU time (`RLIMIT_CPU`) before termination.
- **What the worker (`ai_worker`) cannot do:**
  - Open new files (`openat`), spawn external programs (`execve`), or open network sockets (`socket`/`connect`) once the post-load seccomp filter is active (`Seccomp: 2`, `SECCOMP_RET_KILL_PROCESS`).
  - Elevate privileges via setuid binaries or file capabilities (`CapEff = 0`, `PR_SET_NO_NEW_PRIVS = 1`).
  - Produce core dumps (`RLIMIT_CORE = 0`).
  - Execute arbitrary shell commands directly in `myshell` or hang `myshell` indefinitely (30 s `select()` timeout per read, 16 KiB reply buffer limit).
- **What only the shell (`myshell`) controls:**
  - Interactive prompt parsing, built-in commands (`assist`, `assist-reset`, `help`, `clear`, `exit`), and `fork()`/`execvp()` of external OS commands.
  - Worker lifecycle management: spawning `ai_worker`, redirecting worker `stderr` to `/dev/null` (unless `--verbose`), detecting worker crashes/signals via `waitpid()`, and respawning on subsequent `assist` commands.

---

## Known Limitations

- **No namespace isolation for unprivileged users:** `unshare(CLONE_NEWNS | CLONE_NEWPID)` fails with `EPERM` when run as a normal user without `CAP_SYS_ADMIN` or user-namespace setup.
- **Post-load seccomp scope:** Seccomp is installed after `llama_init_from_model()` opens the model and `/proc/self/status`, and `clone3` remains whitelisted so `llama.cpp` can spawn CPU worker threads during inference. Set `ASSIST_SECCOMP_LOG=1` to run the filter in `SECCOMP_RET_LOG` mode while debugging.
- **Cumulative CPU limit (`RLIMIT_CPU = 600` s):** Prevents infinite runaway loops, meaning a single long-lived worker process will eventually hit the 600 s CPU cap after heavy sustained usage (after which `myshell` reports the signal and respawns a fresh worker on the next `assist` query).
- **Single-turn stateless queries:** Context memory (`llama_memory_clear`) is cleared before every query, so `ai_worker` does not retain multi-turn conversation history across `assist` calls.
- **Small model accuracy & context:** TinyLlama-1.1B-Chat Q4_K_M is a 1.1B parameter quantized model; answers are concise but can be factually imprecise or repetitive on complex prompts.
- **Response size & timeout caps:** `myshell` caps each worker reply at 16,384 bytes (`AI_RESPONSE_MAX`) and times out if a single pipe read stalls for more than 30 s (`AI_TIMEOUT_SEC`).
- **Linux-specific:** Uses `/proc/self/exe`, `/proc/self/status`, `prctl()`, `capset()`, and Linux `rlimit` semantics.

---

## Prerequisites

```bash
sudo apt-get install build-essential cmake libcap-dev
```

*(Note: `libcap-dev` is optional; if absent, `sandbox.c` falls back to the Linux `SYS_capset` syscall directly.)*

---

## Installation & Build

### 1. Clone the repository (with `llama.cpp` submodule)

```bash
git clone --recurse-submodules https://github.com/sharmavaibhav31/offline-shell-assistant.git
cd offline-shell-assistant/mini-shell
```

If already cloned without submodules:

```bash
git submodule update --init --recursive
```

### 2. Download the Model

```bash
mkdir -p ~/.assist_ai
wget https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf -O ~/.assist_ai/model.gguf
md5sum ~/.assist_ai/model.gguf > ~/.assist_ai/model.md5
```

### 3. Build `myshell` and `ai_worker`

Using the Makefile wrapper (invokes CMake):

```bash
cd mini-shell
make
```

Or directly with CMake (optionally passing an existing `llama.cpp` checkout via `-DLLAMA_CPP_DIR`):

```bash
cd mini-shell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
# Optional external llama.cpp:
# cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLLAMA_CPP_DIR=/path/to/llama.cpp
cmake --build build -j$(nproc) --target myshell ai_worker
```

Both `myshell` and `ai_worker` are output into `mini-shell/build/`. `myshell` resolves `ai_worker` relative to its own binary path via `/proc/self/exe`, so it works from any working directory.

---

## Quick Start & Usage

Run the shell:

```bash
./mini-shell/build/myshell
```

Pass `--verbose` (or `-v`) to preserve worker `stderr` and `llama.cpp` logs:

```bash
./mini-shell/build/myshell --verbose
```

If `~/.assist_ai/model.gguf` is missing, `ai_worker` falls back to echo mode (`AI: Echo: <prompt>`).

| Action | Command |
|---|---|
| Ask the AI assistant | `mini-shell> assist what is 2+2` |
| Multi-word prompt | `mini-shell> assist explain the difference between RAM and ROM` |
| Restart the AI worker | `mini-shell> assist-reset` |
| Show built-in commands | `mini-shell> help` |
| Clear screen | `mini-shell> clear` |
| Exit the shell | `mini-shell> exit` |

---

## Performance (Measured)

Measured on `Intel(R) Core(TM) i3-1005G1 CPU @ 1.20GHz` (2 cores / 4 threads, `18 GiB` RAM) running `TinyLlama-1.1B-Chat-v1.0` Q4_K_M (`636.18 MiB`, `n_threads = 2`). See [docs/BENCHMARKS.md](docs/BENCHMARKS.md) for raw commands and full data.

| Metric | Measured Value | Notes |
|---|---|---|
| Token generation (`llama-bench` `tg128`, `-t 2`) | `27.38 ± 1.08 tokens/s` | `pp128`: `86.44 ± 6.58 tokens/s` |
| Peak memory (`/usr/bin/time -v`) | `1,186,008 KB` (~`1.13 GiB`) | Max RSS across 5 queries |
| First query latency | `2.088 s` | Includes worker spawn, model load, and reply |
| Subsequent query latency | `0.963 s` – `10.936 s` | Depends on reply length (up to 256 tokens) |

---

## Testing

```bash
cd mini-shell
./test_assist.sh
./stress_test.sh
./verify_model.sh
```

---

## License

MIT License.
