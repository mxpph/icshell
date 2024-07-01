#ifndef ICSHELL_H
#define ICSHELL_H

#include <sys/types.h>
#include <stdio.h>

#define ICSHELL_NAME        "ICshell"

#define COLORS_ENABLED      (isatty(STDOUT_FILENO))
#define COLOR               "\033[1;96m"
#define COLOR_LEN           (COLORS_ENABLED ? (sizeof(COLOR) - 1) : 0)
#define RESET_COL           "\033[0m"
#define RESET_COL_LEN       (COLORS_ENABLED ? (sizeof(RESET_COL) - 1) : 0)

#define PROMPT_PREFIX       ICSHELL_NAME":"
#define PROMPT_LEN          ((sizeof(PROMPT_PREFIX) - 1) + COLOR_LEN)
#define GNL_BUFFER_SIZE     64
#define INT_STRINGLEN       11 /* max is -2147483648 */

#define EXITCODE(code)      ((code) << 8)

typedef struct gstate_t
{
    int     exitstatus;
} gstate_t;

/* Global */
extern gstate_t gstate;
extern char     **environ;

/* util.c */
void    copy_envp(char **);
void    free_envp(void);
void    printerr(char *);
void    printerr_status(char *, int);
void    error_exit(char *, int);
void    syntax_error(char *);
void    perror_exit(char *, int);
pid_t   fork_and_check(void);
char    *get_next_line(void);
FILE    *fmkstemp(char *);
char    *itoa(int);
void    setup_env(int, char **);
void    custom_puts(char *, int);
char    *current_dir_prompt(void);

/* DEBUG remove when finished */
void    debug_stringlist(char **);

#endif
