#include <stdio.h>
#include <string.h>
#include "shell.h"

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            set_shell_verbose(true);
        }
    }
    shell_loop();  // Start the shell
    return 0;
}

