#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "shell.h"

#define MAX_PROMPT_LEN 4096
#define MAX_RESPONSE_LEN 16384

extern void apply_sandbox(void);
extern void apply_post_load_seccomp(void);

static void send_reply(const char *text, int len) {
    if (len > 0) {
        fwrite(text, 1, (size_t)len, stdout);
        if (text[len - 1] != '\n') {
            fputc('\n', stdout);
        }
    }
    printf("%s\n", AI_END_MARKER);
    fflush(stdout);
}

int echo_worker(void) {
    char prompt[MAX_PROMPT_LEN];
    char reply[MAX_PROMPT_LEN + 16];

    while (fgets(prompt, MAX_PROMPT_LEN, stdin)) {
        prompt[strcspn(prompt, "\n")] = '\0';
        if (strlen(prompt) == 0) {
            send_reply("", 0);
            continue;
        }

        int len = snprintf(reply, sizeof(reply), "Echo: %s", prompt);
        send_reply(reply, len > 0 ? len : 0);
    }

    return 0;
}

#ifdef USE_LLAMA

#include "llama.h"

static void quiet_llama_log(enum ggml_log_level level, const char *text, void *user_data) {
    (void)level;
    (void)text;
    (void)user_data;
}

static void trim_reply(char *buf, int *len) {
    while (*len > 0 && (buf[*len - 1] == '\n' || buf[*len - 1] == '\r' || buf[*len - 1] == ' ')) {
        buf[--(*len)] = '\0';
    }
    char *start = buf;
    while (*start == ' ' || *start == '\n' || *start == '\r') {
        start++;
    }
    if (start != buf) {
        int new_len = *len - (int)(start - buf);
        memmove(buf, start, (size_t)new_len + 1);
        *len = new_len;
    }
}

int llama_worker(const char *model_path, bool verbose) {
    apply_sandbox();

    if (!verbose) {
        llama_log_set(quiet_llama_log, NULL);
    }

    llama_backend_init();

    struct llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = 0;

    struct llama_model *model = llama_model_load_from_file(model_path, model_params);
    if (!model) {
        fprintf(stderr, "Failed to load model\n");
        return 1;
    }

    const struct llama_vocab *vocab = llama_model_get_vocab(model);

    // Create context ONCE and reuse it across queries (cleared between queries)
    struct llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 2048;
    ctx_params.n_batch = 256;
    ctx_params.n_threads = 2;

    struct llama_context *ctx = llama_init_from_model(model, ctx_params);
    if (!ctx) {
        llama_model_free(model);
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }

    // Enable seccomp after model and context are loaded
    apply_post_load_seccomp();

    char prompt[MAX_PROMPT_LEN];
    char formatted_prompt[MAX_PROMPT_LEN + 256];

    while (fgets(prompt, MAX_PROMPT_LEN, stdin)) {
        prompt[strcspn(prompt, "\n")] = '\0';

        if (strlen(prompt) == 0) {
            send_reply("", 0);
            continue;
        }

        // Clear KV cache / memory between queries
        llama_memory_t mem = llama_get_memory(ctx);
        if (mem) {
            llama_memory_clear(mem, true);
        }

        // Format prompt with TinyLlama chat template
        int fmt_len = snprintf(
            formatted_prompt, sizeof(formatted_prompt),
            "<|system|>\nYou are a helpful, concise assistant.</s>\n"
            "<|user|>\n%s</s>\n<|assistant|>\n",
            prompt
        );
        if (fmt_len <= 0 || fmt_len >= (int)sizeof(formatted_prompt)) {
            const char *err = "Error: prompt too long";
            send_reply(err, (int)strlen(err));
            continue;
        }

        // Create sampler for this query
        struct llama_sampler_chain_params sampler_params = llama_sampler_chain_default_params();
        struct llama_sampler *sampler = llama_sampler_chain_init(sampler_params);
        llama_sampler_chain_add(sampler, llama_sampler_init_greedy());

        // Tokenize with parse_special=true so </s> is parsed as EOS token
        int n_prompt_tokens = llama_tokenize(
            vocab, formatted_prompt, fmt_len, NULL, 0, true, true
        );
        if (n_prompt_tokens < 0) n_prompt_tokens = -n_prompt_tokens;

        llama_token *prompt_tokens = malloc((size_t)n_prompt_tokens * sizeof(llama_token));
        if (!prompt_tokens) {
            fprintf(stderr, "Failed to allocate tokens\n");
            const char *err = "Error: memory allocation failed";
            send_reply(err, (int)strlen(err));
            llama_sampler_free(sampler);
            continue;
        }

        int tok_rc = llama_tokenize(
            vocab, formatted_prompt, fmt_len, prompt_tokens, n_prompt_tokens, true, true
        );
        if (tok_rc < 0) {
            free(prompt_tokens);
            const char *err = "Error: tokenization failed";
            send_reply(err, (int)strlen(err));
            llama_sampler_free(sampler);
            continue;
        }

        // Evaluate prompt
        struct llama_batch batch = llama_batch_get_one(prompt_tokens, n_prompt_tokens);
        if (llama_decode(ctx, batch) != 0) {
            fprintf(stderr, "Decode failed\n");
            free(prompt_tokens);
            const char *err = "Error: inference failed";
            send_reply(err, (int)strlen(err));
            llama_sampler_free(sampler);
            continue;
        }

        // Generate response
        char response[MAX_RESPONSE_LEN] = {0};
        int response_len = 0;
        int n_generated = 0;

        while (n_generated < 256) {
            llama_token new_token_id = llama_sampler_sample(sampler, ctx, -1);

            if (llama_vocab_is_eog(vocab, new_token_id)) break;

            char token_buf[32];
            int token_len = llama_token_to_piece(vocab, new_token_id, token_buf, sizeof(token_buf), 0, false);

            if (token_len > 0 && response_len + token_len < MAX_RESPONSE_LEN - 1) {
                memcpy(response + response_len, token_buf, (size_t)token_len);
                response_len += token_len;
                response[response_len] = '\0';

                // Stop if model starts a new chat role turn
                char *stop_pos = strstr(response, "\n<|user|>");
                if (!stop_pos) stop_pos = strstr(response, "\n<|system|>");
                if (stop_pos) {
                    response_len = (int)(stop_pos - response);
                    response[response_len] = '\0';
                    break;
                }
            }

            llama_token next_tokens[1] = {new_token_id};
            batch = llama_batch_get_one(next_tokens, 1);

            if (llama_decode(ctx, batch) != 0) {
                fprintf(stderr, "Decode failed during generation\n");
                break;
            }

            n_generated++;
        }

        trim_reply(response, &response_len);
        send_reply(response, response_len);

        // Cleanup
        free(prompt_tokens);
        llama_sampler_free(sampler);
    }

    // Final cleanup
    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();

    return 0;
}

#endif

int main(int argc, char **argv) {
    bool echo_mode = false;
    bool verbose = false;
    const char *model_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--echo") == 0) {
            echo_mode = true;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else {
            model_path = argv[i];
        }
    }

    if (echo_mode) {
        apply_sandbox();
        apply_post_load_seccomp();
        return echo_worker();
    }

    if (model_path == NULL) {
        fprintf(stderr, "No model path provided, using echo mode\n");
        apply_sandbox();
        apply_post_load_seccomp();
        return echo_worker();
    }

    if (access(model_path, R_OK) != 0) {
        fprintf(stderr, "Model file not accessible, using echo mode\n");
        apply_sandbox();
        apply_post_load_seccomp();
        return echo_worker();
    }

#ifdef USE_LLAMA
    return llama_worker(model_path, verbose);
#else
    fprintf(stderr, "Compiled without llama.cpp support, using echo mode\n");
    apply_sandbox();
    apply_post_load_seccomp();
    return echo_worker();
#endif
}
