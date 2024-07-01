// #define _GNU_SOURCE     /* Stop vscode errors */
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <term.h>
#include <readline/readline.h>
#include <unistd.h>
#include "icshell.h"
#include "signals.h"

void    signals_check_exit(int status, int nl)
{
    /* Don't print "Interrupt" for SIGINT */
    if (WIFSTOPPED(status))
        custom_puts(strsignal(WSTOPSIG(status)), STDERR_FILENO);
    else if (WIFSIGNALED(status))
    {
        if (WTERMSIG(status) != SIGINT)
            custom_puts(strsignal(WTERMSIG(status)), STDERR_FILENO);
    }
    else
        return;
    if (nl)
        fputc('\n', stderr);
}

static void signal_heredoc_cb(int signum)
{
    if (signum == SIGINT)
    {
        close(STDIN_FILENO);
        write(STDERR_FILENO, "^C\n", 3);
        exit(SIGINT + 128);
    }
}

static void signal_default_cb(int signum)
{
    if (signum == SIGQUIT)
        custom_puts("Quit", STDERR_FILENO);
    write(STDERR_FILENO, "\n", 1);
    rl_replace_line("", 0);
    rl_on_new_line();
    rl_redisplay();
    if (signum == SIGINT)
        gstate.exitstatus = SIGINT;
}

static void signal_pipe_cb(int signum)
{
    close(STDIN_FILENO);
    if (signum == SIGQUIT)
        custom_puts("Quit", STDERR_FILENO);
    write(STDERR_FILENO, "\n", 1);
}

/* For some reason, SIG_IGN does not properly block the SIGPIPE
 * signal from being sent to the process, so we use this function.
 * Also, this sets the exit status to 141 (SIGPIPE + 128), which is
 * NOT the default behaviour on bash, but can be replicated by
 * the `set -o pipefail` command.
 */
static void signal_pipe_ign_cb(int signum)
{
    (void)signum;
}

static void setup_sigaction(struct sigaction *sa, int signal,
                            __sighandler_t handler)
{
    sa->sa_handler = handler;
    sa->sa_flags = 0;
    sigemptyset(&sa->sa_mask);
    sigaction(signal, sa, NULL);
}

void    handle_signals(signal_mode_t mode)
{
    struct termios      termattr;
    struct sigaction    sa_int, sa_quit, sa_pipe;
    int                 ret;

    ret = tcgetattr(STDOUT_FILENO, &termattr);
    setup_sigaction(&sa_pipe, SIGPIPE, &signal_pipe_ign_cb);
    switch (mode)
    {
        case NO_MODE:
            termattr.c_lflag |= ECHOCTL; /* echo the ^C, ^\ etc to the screen */
            setup_sigaction(&sa_int,  SIGINT,  SIG_IGN);
            setup_sigaction(&sa_quit, SIGQUIT, SIG_IGN);
            break;
        case INTERACTIVE_MODE:
            termattr.c_lflag |= ECHOCTL;
            setup_sigaction(&sa_int,  SIGINT,  &signal_default_cb);
            setup_sigaction(&sa_quit, SIGQUIT, SIG_IGN);
            break;
        case EXECUTING_MODE:
            termattr.c_lflag |= ECHOCTL;
            setup_sigaction(&sa_int,  SIGINT,  &signal_default_cb);
            setup_sigaction(&sa_quit, SIGQUIT, &signal_default_cb);
            break;
        case HEREDOC_MODE:
            termattr.c_lflag &= ~ECHOCTL; /* do not echo ^C etc to the screen */
            setup_sigaction(&sa_int,  SIGINT,  &signal_heredoc_cb);
            setup_sigaction(&sa_quit, SIGQUIT, SIG_IGN);
            break;
        case INPIPE_MODE:
            termattr.c_lflag |= ECHOCTL;
            setup_sigaction(&sa_int, SIGINT, &signal_pipe_cb);
            setup_sigaction(&sa_quit, SIGQUIT, &signal_pipe_cb);
            break;
    }
    if (!ret)
        tcsetattr(STDOUT_FILENO, TCSANOW, &termattr);
}
