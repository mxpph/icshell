#ifndef PARSE_H
#define PARSE_H

#include "lexer.h"

/* for use with fmkstemp */
#define HEREDOC_FILENAME    "/tmp/icsh_heredoc.XXXXXX"

typedef enum
{
    EXEC,
    REDIR,
    PIPE,
} nodetype_t;

typedef struct parsenode_t parsenode_t;
typedef struct exec_t      exec_t;
typedef struct pipe_t      pipe_t;
typedef struct redir_t     redir_t;

/* argv: list of executable and proceeding arguments */
struct exec_t
{
    char    **argv;
};

struct pipe_t
{
    parsenode_t *left;
    parsenode_t *right;
};

/* file: the file to be opened. */
/* fd: the fd to be dup2'd on (stdin or stdout) */
/* mode: the flags to pass to open() */
/* cmd: pointer to the next node. */
struct redir_t
{
    char        *file;
    int         fd;
    lextype_t   type;
    int         mode;
    parsenode_t *cmd;
};

struct parsenode_t
{
    nodetype_t type;
    union
    {
        pipe_t  *pipe;
        redir_t *redir;
        exec_t  *exec;
    };
};

parsenode_t *parse_create(lexlist_t *);

void        debug_parsetree(parsenode_t *, int);

#endif
