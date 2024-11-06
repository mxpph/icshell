#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include "icshell.h"
#include "lexer.h"
#include "parse.h"
#include "execution.h"
#include "signals.h"
#include "builtins.h"

/* splits the paths on colon and returns a string of paths */
static char **get_paths(void)
{
    char *env_path, *p, **paths;
    int count;

    env_path = getenv("PATH");
    if (!env_path)
        return NULL;
    /* duplicate so that we don't incorrectly modify the real env from strtok */
    env_path = strdup(env_path);
    assert(env_path);
    count = (*env_path != '\0');
    for (char *i = env_path; *i; i++)
    {
        if (*i == ':')
            count++;
    }
    paths = malloc(sizeof(*paths) * (count + 1));
    assert(paths);
    paths[0] = strtok(env_path, ":");
    for (count = 1; (p = strtok(NULL, ":")) != NULL; count++)
        paths[count] = p;
    paths[count] = NULL;
    /* we will free paths[0] to free all the memory pointed to by
     * the pointers in path (because of how strtok works) */
    return paths;
}

/* returns the absolute path if it exists */
static char *in_paths(char *cmd, char  **paths)
{
    char    *full_path;
    int     cmd_len;

    if (!cmd || !*cmd)
        return NULL;
    if (!strncmp(cmd, "./", 2) || strchr(cmd, '/'))
    {
        if (access(cmd, F_OK) != 0)
            perror_exit(cmd, ERROR_NOT_FOUND);
        full_path = strdup(cmd);
        assert(full_path);
        return full_path;
    }
    cmd_len = strlen(cmd);
    if (paths && *cmd != '.')
    {
        for (int i = 0; paths[i]; i++)
        {
            full_path = malloc(strlen(paths[i]) + cmd_len + 2);
            assert(full_path);
            sprintf(full_path, "%s/%s", paths[i], cmd);
            if (access(full_path, F_OK) == 0)
                return full_path;
            free(full_path);
        }
    }
    return NULL;
}

static void exit_if_directory(char *path)
{
    struct stat statbuf;

    if (stat(path, &statbuf) == -1)
        perror_exit("stat", EXIT_FAILURE);
    if (S_ISDIR(statbuf.st_mode)) /* if path is a directory */
    {
        fprintf(stderr, ICSHELL_NAME": %s: is a directory\n", path);
        exit(ERROR_NOT_EXECUTABLE);
    }
}

static void run_exec(exec_t *cmd)
{
    char **paths, *abs_path;

    if (!cmd || !cmd->argv)
        exit(EXIT_SUCCESS);
    builtins_infork(cmd);
    paths = get_paths();
    if ((abs_path = in_paths(cmd->argv[0], paths)) == NULL)
    {
        fprintf(stderr, ICSHELL_NAME": %s: command not found\n", cmd->argv[0]);
        exit(ERROR_NOT_FOUND);
    }
    if (paths)
        free(paths[0]); /* frees all of the strings in paths */
    free(paths);
    if (access(abs_path, X_OK) != 0)
        perror_exit(cmd->argv[0], ERROR_NOT_EXECUTABLE);
    execve(abs_path, cmd->argv, environ);
    /* If execution is successful, this is never reached */
    exit_if_directory(cmd->argv[0]);
    perror_exit(cmd->argv[0], EXIT_FAILURE);
}

/* bit 0 of fd_track is for tracking in (< or <<) redirects
   bit 1 of fd_track is for tracking out (> or >>) redirects */
static void run_redir(redir_t *redir, int fd_track)
{
    int new_fd, type;

    if ((new_fd = open(redir->file, redir->mode, WR_PERMS)) == -1)
    {
        if (redir->cmd && redir->cmd->type == REDIR)
            execute_node(redir->cmd, fd_track);
        perror_exit(redir->file, EXIT_FAILURE);
    }
    if (redir->type == HERE_DOC)
        remove(redir->file); /* delete temporary heredoc file once done */
    type = ((redir->type == REDIR_OUT) || (redir->type == REDIR_APP));
    if (!(fd_track & (1 << type))) /* not duped yet */
    {
        dup2(new_fd, redir->fd);
        fd_track |= 1 << type;
    }
    else
        close(new_fd);
    execute_node(redir->cmd, fd_track);
}

static void run_pipe(pipe_t *cmd, int fd_track)
{
    int     p[2], wstatus;
    pid_t   left, right;

    if (pipe(p) < 0)
        perror_exit("pipe", EXIT_FAILURE);
    handle_signals(INPIPE_MODE);
    left = fork_and_check();
    if (left == 0) /* child process */
    {
        dup2(p[1], STDOUT_FILENO); /* replace stdout with write pipe end */
        close(p[0]);
        close(p[1]);
        execute_node(cmd->left, fd_track);
    }
    right = fork_and_check();
    if (right == 0) /* child process */
    {
        dup2(p[0], STDIN_FILENO); /* replace stdin with read pipe end */
        close(p[0]);
        close(p[1]);
        execute_node(cmd->right, fd_track);
    }
    close(p[0]);
    close(p[1]);
    wstatus = 0; /* default wstatus */
    waitpid(left, NULL, 0);      /* wait for left child to finish */
    waitpid(right, &wstatus, 0); /* wait for right child to finish */
    if (WIFSIGNALED(wstatus) && WTERMSIG(wstatus) != SIGPIPE)
    {
        signals_check_exit(wstatus, 0); /* no extra newline */
        /* bash defines signal exitcodes as starting from 128 */
        exit(WTERMSIG(wstatus) + 128);
    }
    exit(WEXITSTATUS(wstatus));
}

void execute_node(parsenode_t *cmd, int fd_track)
{
    switch (cmd->type)
    {
        case EXEC:
            run_exec(cmd->exec);
            break;
        case REDIR:
            run_redir(cmd->redir, fd_track);
            break;
        case PIPE:
            run_pipe(cmd->pipe, fd_track);
            break;
        default:
            error_exit("unrecognized command", EXIT_FAILURE);
    }
}
