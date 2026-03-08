# Offline AI Assistant Shell (`myshell`)

> An offline AI assistant integrated into a custom C shell (`myshell`), delegating local LLM inference to a separate worker process (`ai_worker`) over Unix pipes using [`llama.cpp`](https://github.com/ggml-org/llama.cpp).

---

## Overview

The **Offline AI Assistant Shell** splits interactive command execution from LLM inference:
- **`myshell` (`main.c`, `shell.c`)**: Parses commands, executes standard shell commands, manages the worker lifecycle (`fork`/`execl`, pipe IPC, bounded reads with timeouts, `waitpid` exit/signal reporting, `assist-reset`), and passes queries to `ai_worker`.
- **`ai_worker` (`ai_worker.c`, `sandbox.c`, `seccomp_filter.c`)**: Runs TinyLlama-1.1B-Chat locally via `llama.cpp` (or falls back to `--echo` mode if no GGUF model is present), applies Linux process limits and privilege reduction, and returns responses framed with `__ASSIST_END__`.

---

## Architecture

```text
+-----------------------+       pipe_to_worker        +---------------------------+
| myshell (Parent)      | --------------------------> | ai_worker (Child Process) |
| - Command loop & exec |                             | - Resource limits         |
| - Bounded timeout I/O | <-------------------------- | - Capability drop         |
| - Respawns on crash   |       pipe_from_worker      | - llama.cpp inference     |
+-----------------------+                             +---------------------------+
```

---

## Prerequisites

```bash
sudo apt-get install build-essential cmake libcap-dev
```

---

## Installation & Build

### 1. Clone the repository (with `llama.cpp` submodule)

```bash
git clone --recurse-submodules https://github.com/sharmavaibhav31/offline-shell-assistant.git
cd offline-shell-assistant/mini-shell
```

If you already cloned without `--recurse-submodules`:

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

Using the Makefile wrapper (calls CMake):

```bash
cd mini-shell
make
```

Or directly with CMake (optionally pointing to an existing `llama.cpp` checkout via `-DLLAMA_CPP_DIR`):

```bash
cd mini-shell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
# Optional external llama.cpp:
# cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLLAMA_CPP_DIR=/path/to/llama.cpp
cmake --build build -j$(nproc) --target myshell ai_worker
```

Both `myshell` and `ai_worker` are built into `mini-shell/build/`. `myshell` locates `ai_worker` next to its own executable via `/proc/self/exe`, so it can be launched from any working directory.

---

## Quick Start & Usage

Run the shell:

```bash
./mini-shell/build/myshell
```

Pass `--verbose` (or `-v`) to keep worker stderr and `llama.cpp` diagnostics visible:

```bash
./mini-shell/build/myshell --verbose
```

If `~/.assist_ai/model.gguf` is missing, `ai_worker` automatically falls back to echo mode (`AI: Echo: <prompt>`).

| Action | Command |
|--------|---------|
| Ask the AI assistant | `mini-shell> assist what is 2+2` |
| Multi-word prompt | `mini-shell> assist explain the difference between RAM and ROM` |
| Restart the AI worker | `mini-shell> assist-reset` |
| Show built-in commands | `mini-shell> help` |
| Clear screen | `mini-shell> clear` |
| Exit the shell | `mini-shell> exit` |

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
