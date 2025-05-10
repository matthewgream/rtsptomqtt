
// -----------------------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------------------

#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

// -----------------------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------------------

size_t exec(const char *command, const char *const arguments[], unsigned char *data, const size_t size) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return 0;
    }
    pid_t pid = fork();
    if (pid == -1) { // Error
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return 0;
    }
    if (pid == 0) { // Child process
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        const int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        close(pipefd[1]);
        execvp(command, (char *const *)arguments);
        perror("execvp");
        exit(EXIT_FAILURE);
    }
    // Parent process
    close(pipefd[1]);
    size_t total_bytes = 0;
    ssize_t bytes_read;
    while ((bytes_read = read(pipefd[0], data + total_bytes, size - total_bytes)) > 0) {
        total_bytes += bytes_read;
        if (total_bytes >= size) {
            fprintf(stderr, "command (%s) data too large for buffer\n", command);
            total_bytes = 0;
            break;
        }
    }
    close(pipefd[0]);
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        fprintf(stderr, "command (%s) exited with status %d\n", command, WEXITSTATUS(status));
        return 0;
    }
    return total_bytes;
}

// -----------------------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------------------
