#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <sys/wait.h>
#include "icshell.h"
#include "lexer.h"
#include "builtins.h"
#include "parse.h"

static int invalid_syntax(lexeme_t *argv)
{
    if (argv && argv->type != WORD)
    {
        fprintf(stderr,
            ICSHELL_NAME": syntax error in builtin: unexpected token `%s'\n",
            argv->content);
        gstate.exitstatus = EXITCODE(EXIT_INVALID_BUILTIN);
        return 1;
    }
    return 0;
}

void set_pwd(char *key)
{
    char    *cwd;

    cwd = getcwd(NULL, 0);
    if (!cwd)
        error_exit("cd: error retrieving current directory: getcwd",
                        EXIT_FAILURE);
    setenv(key, cwd, 1);
    free(cwd);
}

static void builtins_cd(lexeme_t *argv)
{
    char    *dir;

    if (invalid_syntax(argv))
        return;
    if (argv && argv->next)
    {
        printerr_status("cd: too many arguments", EXIT_FAILURE);
        return;
    }
    /* 'cd' means 'cd $HOME' */
    if (!argv || !strcmp(argv->content, "~"))
    {
        dir = getenv("HOME");
        if (!dir || !*dir)
        {
            printerr_status("cd: HOME not set", EXIT_FAILURE);
            return;
        }
    }
    else if (!strcmp(argv->content, "-"))
    {
        dir = getenv("OLDPWD");
        if (!dir || !*dir)
        {
            printerr_status("cd: OLDPWD not set", EXIT_FAILURE);
            return;
        }
        fputs(dir, stdout);
        fputc('\n', stdout);
    }
    else
        dir = argv->content;
    set_pwd("OLDPWD");
    if (chdir(dir) == -1)
    {
        fprintf(stderr, ICSHELL_NAME": cd: %s: ", dir);
        perror(NULL);
        gstate.exitstatus = EXITCODE(EXIT_FAILURE);
        return;
    }
    set_pwd("PWD");
    gstate.exitstatus = EXITCODE(EXIT_SUCCESS);
}

static void builtins_pwd(void)
{
    char    *cwd;

    cwd = getcwd(NULL, 0);
    if (!cwd)
    {
        error_exit("pwd: error retrieving current directory: getcwd",
                        EXIT_FAILURE);
    }
    fputs(cwd, stdout);
    fputc('\n', stdout);
    free(cwd);
    exit(EXIT_SUCCESS);
}

static void builtins_env(char **argv)
{
    char    **p, *equals;

    if (*argv)
        error_exit("env: too many arguments", EXIT_FAILURE);
    for (p = environ; *p; p++)
    {
        if (isdigit(**p))
            continue;
        equals = strchr(*p, '=');
        if (equals && equals[1]) /* only print if the variable is set */
        {
            fputs(*p, stdout);
            fputc('\n', stdout);
        }
    }
    exit(EXIT_SUCCESS);
}

static int n_flag(char *p)
{

    if (*p != '-')
        return 0;
    p++;
    while (*p)
    {
        if (*p != 'n')
            return 0;
        p++;
    }
    return 1;
}

static void builtins_echo(char **argv)
{
    int     done = 0; /* no more arguments */
    int     nflag = 0;

    while (*argv)
    {
        if (done || !n_flag(*argv))
        {
            done = 1;
            fputs(*argv, stdout);
            if (argv[1])
                fputc(' ', stdout);
        }
        else
            nflag = 1;
        argv++;
    }
    if (!nflag)
        fputc('\n', stdout);
    exit(EXIT_SUCCESS);
}

static int  key_is_valid(char *key)
{
    if (!isalpha(*key) && *key != '_')
        return 0;
    while (*key)
    {
        if (!isalnum(*key) && *key != '_')
            return 0;
        key++;
    }
    return 1;
}

/* Get key, value from 'key=value'. Returns key, sets *value */
static char *parse_key(char *p, char **value)
{
    char *res, *pos;

    res = strdup(p);
    pos = strchr(res, '=');
    if (!pos) /* no equals */
    {
        if (value)
            *value = NULL;
        return res;
    }
    else
        *pos = '\0';
    if (value)
        *value = pos + 1;
    return res;
}

static int compare_strings(const void *s1, const void *s2)
{
    return strcmp(*(const char **)s1, *(const char **)s2);
}

static char **copy_environ(int len)
{
    char    **envp;
    int     i;

    if (!environ)
    {
        envp = malloc(sizeof(char *));
        assert(envp);
        *envp = NULL;
        return envp;
    }
    envp = malloc(sizeof(char *) * (len + 1));
    assert(envp);
    for (i = 0; i < len; i++)
        envp[i] = strdup(environ[i]);
    envp[len] = NULL;
    return envp;
}


static void builtins_export(lexeme_t *argv)
{
    char    *key, *value, **env, **environ_copy;
    int     len, exitstatus;

    exitstatus = EXIT_SUCCESS;
    if (!argv)
    {
        if (!environ)
            return;
        for (len = 0; environ[len]; len++)
            /* DO NOTHING */;
        environ_copy = copy_environ(len);
        qsort(environ_copy, len, sizeof(*environ_copy), &compare_strings);
        for (env = environ_copy; *env; env++)
        {
            key = parse_key(*env, &value);
            if (!isdigit(*key))
            {
                if (!value || !*value)
                    printf("declare -x %s\n", key);
                else
                    printf("declare -x %s=\"%s\"\n", key, value);
            }
            free(key);
            free(*env);
        }
        free(*env);
        free(environ_copy);
    }
    while (argv)
    {
        if (invalid_syntax(argv))
            return;
        key = parse_key(argv->content, &value);
        if (key_is_valid(key))
            setenv(key, value ? value : "", 1);
        else
        {
            fprintf(stderr,
                ICSHELL_NAME": export: `%s': not a valid identifier\n",
                argv->content);
            exitstatus = EXIT_FAILURE;
        }
        free(key);
        argv = argv->next;
    }
    gstate.exitstatus = EXITCODE(exitstatus);
}

static void builtins_unset(lexeme_t *argv)
{
    char    *content;

    while (argv)
    {
        if (invalid_syntax(argv))
            return;
        content = argv->content;
        if (content && *content)
            unsetenv(content);
        argv = argv->next;
    }
    gstate.exitstatus = EXITCODE(EXIT_SUCCESS);
}

static void builtins_exit(lexeme_t *argv)
{
    char        *endptr, *str;
    int64_t     code;

    if (invalid_syntax(argv))
        return;
    /* Some tests fail because of this but it is accurate bash behaviour
     * if done by hand and not with the tester. */
    fputs("exit\n", stderr);
    if (argv)
    {
        if (argv->next)
        {
            printerr_status("exit: too many arguments", EXIT_FAILURE);
            return;
        }
        str = argv->content;
        code = strtoll(str, &endptr, 10);
        if (*endptr == '\0'
            && !(( code == LLONG_MAX
                || code == LLONG_MIN)
                && errno == ERANGE)) /* valid conversion */
        {
            exit(code & 0xff); /* mod 256 */
        }
        fprintf(stderr,
            ICSHELL_NAME": exit: %s: numeric argument required\n", str);
        exit(EXIT_INVALID_BUILTIN);
    }
    exit(WEXITSTATUS(gstate.exitstatus));
}

/* These builtins can be done in the fork because they do not
 * require modifying the internal state of the shell, i.e.
 * the environment or working directory. */
void    builtins_infork(exec_t *exec)
{
    char    *cmd;

    if (!exec->argv)
        return;
    cmd = exec->argv[0];
    if (!strcmp(cmd, "pwd"))
        builtins_pwd();
    else if (!strcmp(cmd, "echo"))
        builtins_echo(exec->argv + 1);
    else if (!strcmp(cmd, "env"))
        builtins_env(exec->argv + 1);
}

/* Return 1 if it was a builtin otherwise 0 */
int builtins_handle(lexeme_t *head)
{
    char    *cmd;

    if (head->type != WORD)
        return 0;
    cmd = head->content;
    if (!strcmp(cmd, "cd"))
        builtins_cd(head->next);
    else if (!strcmp(cmd, "export"))
        builtins_export(head->next);
    else if (!strcmp(cmd, "unset"))
        builtins_unset(head->next);
    else if (!strcmp(cmd, "exit"))
        builtins_exit(head->next);
    else
        return 0;
    return 1;
}
