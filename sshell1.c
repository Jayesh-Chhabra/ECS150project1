#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <ctype.h>

#define CMDLINE_MAX 512


void first_word(const char *str, char *buffer) {
    // Skip leading whitespace
    while (isspace(*str)) {
        str++;
    }

    // Copy the first word
    while (*str != '\0' && !isspace(*str)) {
        *buffer = *str;
        buffer++;
        str++;
    }

    // Null-terminate the buffer
    *buffer = '\0';
}

void parse_command(char *cmd, char **args) {
    int i = 0;
    char *token = strtok(cmd, " ");
    // loop over the input command string, and use `strtok` to split it
    // into individual tokens, separated by spaces
    while (token != NULL && i < CMDLINE_MAX) {
        // store each token in the `args` array
        args[i] = token;
        i++;
        // get the next token
        token = strtok(NULL, " ");
    }
    // set the last element in `args` to NULL, as expected by `execvp`
    args[i] = NULL;
}
// counts the number of pipes
int count_pipes(char *cmd) {
    int count = 0;
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '|') {
            count++;
        }
    }
    return count;
}

void execute_pipeline(char *cmd) {
    // Count the number of pipes in the command
    int num_pipes = count_pipes(cmd);

    // Calculate the number of commands based on the number of pipes
    int num_cmds = num_pipes + 1;

    // Split the command into individual commands based on the pipes
    char **commands = (char **)malloc((num_cmds + 1) * sizeof(char *));
    char *token = strtok(cmd, "|");
    int i = 0;
    while (token != NULL && i < num_cmds) {
        commands[i] = token;
        i++;
        token = strtok(NULL, "|");
    }
    commands[i] = NULL;

    // Create an array of file descriptors for the pipes
    int pipe_fds[2 * num_pipes];

    // Create the pipes and check for errors
    for (i = 0; i < num_pipes; i++) {
        if (pipe(pipe_fds + i * 2) < 0) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    // Save the current stdout file descriptor
    int last_stdout = dup(STDOUT_FILENO);

    // Loop over each command and execute it in a child process
    for (i = 0; i < num_cmds; i++) {
        // Create a new child process to execute the command
        pid_t pid = fork();

        // Check for errors in fork()
        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        // In the child process, execute the command
        if (pid == 0) {
            // Parse the command into an array of arguments
            char *single_cmd = commands[i];
            char **args = (char **)malloc((CMDLINE_MAX + 1) * sizeof(char *));
            parse_command(single_cmd, args);

            // Redirect input from the previous command's output, if necessary
            if (i != 0) {
                if (dup2(pipe_fds[(i - 1) * 2], STDIN_FILENO) < 0) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }

            // Redirect output to the next command's input, if necessary
            if (i != num_cmds - 1) {
                if (dup2(pipe_fds[i * 2 + 1], STDOUT_FILENO) < 0) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }

            // Close all pipe endpoints in this child process
            for (int j = 0; j < 2 * num_pipes; j++) {
                close(pipe_fds[j]);
            }

            // Execute the command
            if (execvp(args[0], args) < 0) {
                perror(args[0]);
                exit(EXIT_FAILURE);
            }
        }
    }

    // Close all pipe endpoints in the parent process
    for (int j = 0; j < 2 * num_pipes; j++) {
        close(pipe_fds[j]);
    }

    // Restore stdout and close last_stdout
    dup2(last_stdout, STDOUT_FILENO);
    close(last_stdout);

    // Wait for all child processes to complete
    for (int j = 0; j < num_cmds; j++) {
        wait(NULL);
    }

    // Free memory allocated for commands
    free(commands);
}

int main(void) {
    char cmd[CMDLINE_MAX];
    while (1) {
        char *nl;
        int retval = 0;

        /* Print prompt */
        printf("sshell$ ");
        fflush(stdout);

        /* Get command line */
        fgets(cmd, CMDLINE_MAX, stdin);

        /* Print command line if stdin is not provided by terminal */
        if (!isatty(STDIN_FILENO)) {
            printf("%s", cmd);
            fflush(stdout);
        }

        /* Remove trailing newline from command line */
        nl = strchr(cmd, '\n');
        if (nl) {
            *nl = '\0';
        }

        /* Builtin command */
        if (!strcmp(cmd, "exit")) {
            fprintf(stderr, "Bye...\n");
            break;
        }
        char first_word_buffer[CMDLINE_MAX];
        first_word(cmd, first_word_buffer);
        if (!strcmp(first_word_buffer, "cd")) {
            char *dir = strchr(cmd, ' ');
            if (dir) {
                while (isspace(*dir)) {
                    dir++;
                }
                if (chdir(dir) < 0) {
                    perror(dir);
                    retval = 1;
                }
            } else {
                chdir(getenv("HOME"));
            }
        } else {
            /* Regular command */
            execute_pipeline(cmd);
        }

        fprintf(stdout, "Return status value for '%s': %d\n", cmd, retval);
    }

    return EXIT_SUCCESS;
}