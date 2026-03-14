# Benchmarks

All measurements below were collected locally on the target machine using `llama-bench` (from `llama.cpp` commit `56fc38b`), `/usr/bin/time -v`, and wall-clock timing against `./mini-shell/build/myshell`.

## Hardware & Software Environment

- **CPU:** `Intel(R) Core(TM) i3-1005G1 CPU @ 1.20GHz` (2 physical cores, 4 hardware threads)
- **RAM:** `18 GiB` total (`free -h`)
- **Model:** `TinyLlama-1.1B-Chat-v1.0` (`Q4_K_M`, `636.18 MiB`, 1.10 B parameters) at `~/.assist_ai/model.gguf`
- **Worker config:** `n_ctx = 2048`, `n_batch = 256`, `n_threads = 2`, CPU-only (`n_gpu_layers = 0`), max 256 generated tokens per query

## Throughput (`llama-bench`)

Command:
```bash
llama-bench -m ~/.assist_ai/model.gguf -t 2,4 -p 128 -n 128 -r 3
```

| Threads | Test | Throughput (tokens/s) |
|---|---|---|
| 2 (used by `ai_worker`) | `pp128` (prompt eval) | `86.44 ± 6.58` |
| 2 (used by `ai_worker`) | `tg128` (token generation) | `27.38 ± 1.08` |
| 4 | `pp128` (prompt eval) | `81.87 ± 3.69` |
| 4 | `tg128` (token generation) | `25.57 ± 0.68` |

## Peak Memory (`/usr/bin/time -v`)

Command (5 consecutive queries piped into `ai_worker`):
```bash
printf 'what is 2+2\nwhat is the capital of India\nwhat is recursion\nwhat is HTTP\nwhat is Linux\n' \
  | /usr/bin/time -v ./mini-shell/build/ai_worker ~/.assist_ai/model.gguf
```

- **Maximum resident set size (Peak RSS):** `1,186,008 KB` (~`1,158 MiB` / ~`1.13 GiB`)
- **Elapsed wall time (5 queries):** `22.99 s` (`49.23 s` user, `0.39 s` system)

## Query Latency (`myshell` end-to-end)

Measured with a warm OS page cache from sending `assist <query>` until the `mini-shell> ` prompt returned:

| Query | Latency | Notes |
|---|---|---|
| Q1: `assist what is 2+2` | `2.088 s` | Includes worker `fork`/`exec`, `apply_sandbox()`, model load, context creation, and generation |
| Q2: `assist what is the capital of India` | `0.963 s` | Reuses loaded model/context (`llama_memory_clear`), short reply |
| Q3: `assist what is recursion in one sentence` | `1.485 s` | Reuses loaded model/context |
| Q4: `assist what does ls do in Linux` | `10.936 s` | Long generation hitting 256-token cap |
| Q5: `assist what is RAM` | `5.525 s` | Multi-sentence reply |
| Q6: `assist what is HTTP` | `5.115 s` | Multi-sentence reply |
