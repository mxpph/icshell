#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>
#include <ncurses.h>
#include <term.h>
#include <string.h>
#include "icshell.h"
#include "builtins.h"

void    debug_stringlist(char **p)
{
    if (p)
    {
        while (*p)
            fprintf(stderr, "%s\n", *p++);
    }
}

void    printerr(char *s)
{
    fprintf(stderr, ICSHELL_NAME": %s\n", s);
}

void    printerr_status(char *s, int status)
{
    printerr(s);
    gstate.exitstatus = EXITCODE(status);
}

void    error_exit(char *s, int code)
{
    printerr(s);
    exit(code);
}

void    syntax_error(char *s)
{
    fputs(ICSHELL_NAME": syntax error near unexpected token `", stderr);
    if (s && *s)
        fputs(s, stderr);
    else
        fputs("newline", stderr);
    fputs("\'\n", stderr);
    exit(EXIT_INVALID_BUILTIN);
}

void    perror_exit(char *s, int code)
{
    fputs(ICSHELL_NAME": ", stderr);
    perror(s);
    exit(code);
}

pid_t   fork_and_check(void)
{
    pid_t   pid;

    pid = fork();
    if (pid == -1)
        perror_exit("fork", EXIT_FAILURE);
    return pid;
}

/* gets next line from stdin up to and including newline or EOF character */
char *get_next_line(void)
{
    char    c, *line;
    int     len, cap;

    line = malloc(GNL_BUFFER_SIZE);
    assert(line);
    len = 0;
    cap = GNL_BUFFER_SIZE;
    while ((c = getchar()) != EOF)
    {
        if (len + 1 >= cap)
        {
            cap *= 2;
            line = realloc(line, cap);
            assert(line);
        }
        line[len++] = c;
        if (c == '\n')
            break;
    }
    if (len == 0)
    {
        free(line);
        return NULL;
    }
    line[len] = '\0';
    return line;
}

/* create temporary file, see man mkstemp */
FILE    *fmkstemp(char *template)
{
    FILE    *fp;
    int     fd;

    fp = NULL;
    fd = mkstemp(template);
    if (fd >= 0)
    {
        fp = fdopen(fd, "w+");
        if (!fp)
        {
            close(fd);
            remove(template); /* delete the file */
        }
    }
    return(fp);
}

char    *itoa(int n)
{
    char    *value;

    value = malloc(INT_STRINGLEN + 1);
    assert(value);
    snprintf(value, INT_STRINGLEN + 1, "%d", n);
    return value;
}


static void set_shlvl(void)
{
    char    new[INT_STRINGLEN], *val, *endptr;
    long    shlvl;

    val = getenv("SHLVL");
    if (val)
    {
        shlvl = strtol(val, &endptr, 10);
        if (!*endptr)
        {
            snprintf(new, sizeof(new), "%ld", shlvl + 1);
            setenv("SHLVL", new, 1);
            return;
        }
    }
    setenv("SHLVL", "1", 1);
    return;
}

static void setupterm_wrapper(char *term)
{
    int ret, errret;

    ret = setupterm(term, STDOUT_FILENO, &errret);
    if (ret == ERR && !errret)
    {
        fprintf(stderr, ICSHELL_NAME": can't find terminal definition for %s\n",
            term ? term : "(null)");
    }
}

void    setup_env(int argc, char **argv)
{
    char    *digit;

    for (int i = 0; i < argc; i++)
    {
        digit = itoa(i);
        setenv(digit, argv[i], 1);  /* $0, $1, ... */
        free(digit);
    }
    set_shlvl();
    set_pwd("PWD");
    setenv("OLDPWD", "", 0);    /* don't overwrite */
    setenv("TERM", "linux", 0); /* don't overwrite */
    setenv("SHELL", "icshell", 1); /* overwrite */
    setupterm_wrapper(getenv("TERM"));
}

/* required because FILE functions (e.g fputs) are buffered
 * internally which causes bugs within signal handler */
void    custom_puts(char *s, int fd)
{
    if (s)
    {
        while (*s)
            write(fd, s++, 1);
    }
}

char    *current_dir_prompt(void)
{
    char    *ret, *pwd, *home_dir;
    int     pwd_len, home_len;

    pwd      = getenv("PWD");
    home_dir = getenv("HOME");
    if (!pwd)
    {
        ret = strdup(ICSHELL_NAME"> ");
        assert(ret);
        return ret;
    }
    pwd_len = strlen(pwd);
    /* +4 allocates space for '/' and "> \0" */
    ret = malloc(PROMPT_LEN + pwd_len + RESET_COL_LEN + 4);
    assert(ret);
    strcpy(ret, PROMPT_PREFIX);
    if (COLORS_ENABLED)
        strcat(ret, COLOR);
    strcpy(ret + PROMPT_LEN, pwd);
    if (home_dir && *home_dir)
    {
        home_len = strlen(home_dir);
        if (!strncmp(pwd, home_dir, home_len))
        {
            ret[PROMPT_LEN] = '~';
            if (home_dir[home_len - 1] == '/')
                home_len--;
            strcpy(ret + PROMPT_LEN + 1, pwd + home_len);
        }
    }
    if (COLORS_ENABLED)
        strcat(ret, RESET_COL);
    strcat(ret, "> ");
    return ret;
}
