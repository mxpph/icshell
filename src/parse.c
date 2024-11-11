#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "icshell.h"
#include "lexer.h"
#include "parse.h"
#include "signals.h"

/* checks the lexeme given to be the same as the type. Returns 0 if not. */
static int peek(lexeme_t **list, lextype_t type)
{
    lexeme_t    *lex;

    lex = *list;
    if (lex)
        return lex->type & type;
    return 0;
}

/* returns a pointer to the current node, and alters it to the next node */
static lexeme_t *take(lexeme_t **list)
{
    lexeme_t    *temp;

    temp = *list;
    if (!temp)
        return NULL;
    *list = temp->next;
    return temp;
}

static parsenode_t *new_execnode(void)
{
    parsenode_t *new;

    new = malloc(sizeof(*new));
    assert(new);
    new->type = EXEC;
    new->exec = calloc(1, sizeof(*new->exec));
    assert (new->exec);
    return new;
}

static parsenode_t *new_redirnode(char *file, int fd, lextype_t type, int mode,
                                  parsenode_t *cmd)
{
    parsenode_t *new;

    assert(file);
    new = malloc(sizeof(*new));
    assert(new);
    new->type = REDIR;
    new->redir = malloc(sizeof(*new->redir));
    assert(new->redir);
    new->redir->file = file;
    new->redir->fd = fd;
    new->redir->type = type;
    new->redir->mode = mode;
    new->redir->cmd = cmd;
    return new;
}

static parsenode_t *new_pipenode(parsenode_t *left, parsenode_t *right)
{
    parsenode_t *new;

    new = malloc(sizeof(*new));
    assert(new);
    new->type = PIPE;
    new->pipe = malloc(sizeof(*new->pipe));
    assert(new->pipe);
    new->pipe->left = left;
    new->pipe->right = right;
    return new;
}

static void next_exec_arg(parsenode_t *cmd, lexeme_t *lexeme)
{
    int argc;

    while (cmd->type == REDIR)
        cmd = cmd->redir->cmd;
    argc = 0;
    if (cmd->exec->argv)
    {
        while (cmd->exec->argv[argc])
            argc++;
    }
    cmd->exec->argv = realloc(cmd->exec->argv, sizeof(char *) * (argc + 2));
    cmd->exec->argv[argc] = lexeme->content;
    cmd->exec->argv[argc + 1] = NULL;
}

static parsenode_t *parse_heredoc(char *delim, parsenode_t *scmd)
{
    int         dlen;
    char        *line;
    char        tmpfname[] = HEREDOC_FILENAME; /* see man mkstemp */
    FILE        *heredoc;
    lexlist_t   *lexline;

    heredoc = fmkstemp(tmpfname); /* edits the XXXXXX to something unique */
    if (!heredoc)
        perror_exit("heredoc", EXIT_FAILURE);
    handle_signals(HEREDOC_MODE);
    dlen = strlen(delim);
    fputs("> ", stdout);
    line = get_next_line();
    while (line)
    {
        if (!strncmp(line, delim, dlen) && (!line[dlen] || line[dlen] == '\n'))
            break;
        else
        {
            lexline = lexer_create(line);
            lexer_env_expand(lexline);
            for (lexeme_t *cur = lexline->head; cur; cur = cur->next)
                fputs(cur->content, heredoc);
            lexlist_free(lexline);
        }
        free(line);
        fputs("> ", stdout);
        line = get_next_line();
    }
    free(line);
    fclose(heredoc);
    handle_signals(EXECUTING_MODE);
    return new_redirnode(strdup(tmpfname), STDIN_FILENO, HERE_DOC,
                         O_RDONLY, scmd);
}

/* REDIRNODE ::= [REDIR_IN | REDIR_OUT | HERE_DOC | REDIR_APP] WORD [REDIRNODE]
 */
static parsenode_t *parse_redir(parsenode_t *cmd, lexeme_t **cur)
{
    lexeme_t    *redir, *next;

    while (peek(cur, REDIR_IN | REDIR_OUT | HERE_DOC | REDIR_APP))
    {
        redir = take(cur);
        next = take(cur);
        if (!next || next->type != WORD || !*next->content)
            syntax_error(next ? next->content : NULL);
        switch (redir->type)
        {
            case REDIR_IN:
                cmd = new_redirnode(next->content, STDIN_FILENO, REDIR_IN,
                                    O_RDONLY, cmd);
                break;
            case REDIR_OUT:
                cmd = new_redirnode(next->content, STDOUT_FILENO, REDIR_OUT,
                                    O_WRONLY | O_CREAT | O_TRUNC, cmd);
                break;
            case HERE_DOC:
                cmd = parse_heredoc(next->content, cmd);
                break;
            case REDIR_APP:
                cmd = new_redirnode(next->content, STDOUT_FILENO, REDIR_APP,
                                    O_WRONLY | O_CREAT | O_APPEND, cmd);
                break;
            default: /* should be impossible to end up here */
                error_exit("unexpected type when parsing redirection",
                           EXIT_FAILURE);
        }
    }
    return cmd;
}


/* EXECNODE ::= [REDIRNODE] WORD+ [REDIRNODE] */
static parsenode_t *parse_exec(lexeme_t **cur)
{
    int         argc;
    parsenode_t *cmd;
    lexeme_t    *lexeme;

    cmd = new_execnode();
    cmd = parse_redir(cmd, cur);
    argc = 0;
    while (!peek(cur, PIPELINE))
    {
        lexeme = take(cur);
        if (!lexeme)
            break;
        if (lexeme->type != WORD)
            syntax_error(lexeme->content);
        if (cmd->type == EXEC)
        {
            cmd->exec->argv = realloc(cmd->exec->argv,
                                      sizeof(char *) * (argc + 2));
            cmd->exec->argv[argc] = lexeme->content;
            cmd->exec->argv[argc + 1] = NULL;
        }
        else if (cmd->type == REDIR)
            next_exec_arg(cmd, lexeme);
        argc++;
        cmd = parse_redir(cmd, cur);
    }
    return cmd;
}

/* PIPENODE ::= EXECNODE | EXECNODE PIPELINE PIPENODE */
static parsenode_t *parse_pipe(lexeme_t **cur)
{
    parsenode_t *node;

    node = parse_exec(cur);
    if (peek(cur, PIPELINE))
    {
        take(cur);
        if (!*cur || peek(cur, PIPELINE)
            || (node->type == EXEC && !node->exec->argv))
        {
            syntax_error("|");
        }
        node = new_pipenode(node, parse_pipe(cur));
    }
    return node;
}

parsenode_t *parse_create(lexlist_t *lexemes)
{
    lexeme_t    *cur;

    cur = lexemes->head;
    return (parse_pipe(&cur));
}

void    debug_parsetree(parsenode_t *node, int depth)
{
    if (!node)
        return;
    if (node->type == PIPE)
    {
        debug_parsetree(node->pipe->right, depth + 4);
        for (int i = 0; i < depth; i++)
            fputc(' ', stderr);
        fputs("PIPE\n", stderr);
        debug_parsetree(node->pipe->left, depth + 4);
    }
    else if (node->type == REDIR)
    {
        for (int i = 0; i < depth; i++)
            fputc(' ', stderr);
        fprintf(stderr, "REDIR(%s)-->", node->redir->file);
        debug_parsetree(node->redir->cmd, 0);
    }
    else if (node->type == EXEC)
    {
        for (int i = 0; i < depth; i++)
            fputc(' ', stderr);
        fputs("EXEC:", stderr);
        for (int i = 0; node->exec->argv[i]; i++)
            fprintf(stderr,"%s ", node->exec->argv[i]);
        fputc('\n', stderr);
    }
}
