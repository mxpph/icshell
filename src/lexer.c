#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include "icshell.h"
#include "lexer.h"

static void handle_quotes(lexeme_t *lex, lextype_t type, qstate_t *qstate)
{
    if (type == SQUOTE)
    {
        if (*qstate == IN_SQUOTE)
        {
            lex->qstate = NOQUOTE;
            *qstate = NOQUOTE;
        }
        else if (*qstate == NOQUOTE)
            *qstate = IN_SQUOTE;
    }
    else if (type == DQUOTE)
    {
        if (*qstate == IN_DQUOTE)
        {
            lex->qstate = NOQUOTE;
            *qstate = NOQUOTE;
        }
        else if (*qstate == NOQUOTE)
            *qstate = IN_DQUOTE;
    }
}

lexeme_t    *new_lexeme(char *content, uint32_t len, lextype_t type,
                        qstate_t *qstate)
{
    lexeme_t    *lex;

    lex = calloc(1, sizeof(*lex));
    if (!lex)
        return (NULL);
    lex->content = strdup(content);
    if (!lex->content)
    {
        free(lex);
        return (NULL);
    }
    lex->len = len;
    lex->type = type;
    lex->qstate = *qstate;
    handle_quotes(lex, type, qstate);
    return lex;
}

static lexeme_t *handle_whitespace(char *s, qstate_t *qstate)
{
    uint32_t    i;
    char        c;
    lexeme_t    *cur;

    for (i = 0; isspace(s[i]); i++)
        /* DO NOTHING*/;
    c = s[i];    /* save the character so we can put it back after*/
    s[i] = '\0';
    cur = new_lexeme(s, i, WHITESPACE, qstate);
    s[i] = c;
    return cur;
}

static lexeme_t *handle_symbols(char *s, qstate_t *qstate)
{
    lexeme_t    *cur;

    cur = NULL;
    switch (*s)
    {
        case '\'':
            cur = new_lexeme("\'", 1, SQUOTE, qstate);
            break;
        case '\"':
            cur = new_lexeme("\"", 1, DQUOTE, qstate);
            break;
        case '|':
            cur = new_lexeme("|", 1, PIPELINE, qstate);
            break;
        case '>':
            if (s[1] == '>')
                cur = new_lexeme(">>", 2, REDIR_APP, qstate);
            else
                cur = new_lexeme(">", 1, REDIR_OUT, qstate);
            break;
        case '<':
            if (s[1] == '<')
                cur = new_lexeme("<<", 2, HERE_DOC, qstate);
            else
                cur = new_lexeme("<", 1, REDIR_IN, qstate);
            break;
    }
    return cur;
}

static lexeme_t *handle_words(char *s, qstate_t *qstate)
{
    lextype_t   type;
    uint32_t    i;
    char        c;
    lexeme_t    *cur;

    if (s[0] == '$' && (isalnum(s[1]) || strchr("_?$", s[1])))
    {
        type = ENV;
        i = 2;
        if (isalpha(s[1]) || s[1] == '_')
        {
            while (isalnum(s[i]) || s[i] == '_')
                ++i;
        }
    }
    else
    {
        type = WORD;
        i = (s[0] == '$');
        while (s[i] && !isspace(s[i]) && !strchr("><\'\"|$", s[i]))
            ++i;
    }
    c = s[i];
    s[i] = '\0';
    cur = new_lexeme(s, i, type, qstate);
    s[i] = c;
    return cur;
}


void    list_add_tail(lexlist_t *list, lexeme_t *new)
{
    if (!list->head)
        list->head = new;
    else
    {
        list->tail->next = new;
        new->prev = list->tail;
    }
    list->tail = new;
}

void    lexlist_free(lexlist_t *list)
{
    lexeme_t    *temp, *temp2;

    temp = list->head;
    while (temp)
    {
        free(temp->content);
        temp2 = temp->next;
        free(temp);
        temp = temp2;
    }
    free(list);
}

lexlist_t   *lexer_create(char *s)
{
    lexlist_t   *list;
    lexeme_t    *cur;
    qstate_t    qstate;

    list = calloc(1, sizeof(*list));
    assert(list);
    qstate = NOQUOTE;
    while (*s)
    {
        cur = NULL;
        if (isspace(*s))
            cur = handle_whitespace(s, &qstate);
        else if (strchr("><\'\"|", *s))
            cur = handle_symbols(s, &qstate);
        else
            cur = handle_words(s, &qstate);
        list_add_tail(list, cur);
        s += cur->len;
    }
    return list;
}

static char *getenv_withexit(char *key)
{
    char    *value;
    int     intval;

    if (!*key)
    {
        value = strdup("$");
        assert(value);
        return value;
    }
    if (*key == '?')
    {
        if (WIFEXITED(gstate.exitstatus))
            intval = WEXITSTATUS(gstate.exitstatus);
        else if (WIFSIGNALED(gstate.exitstatus))
            intval = WTERMSIG(gstate.exitstatus) + 128;
        else if (WIFSTOPPED(gstate.exitstatus))
            intval = WSTOPSIG(gstate.exitstatus) + 128;
        else
            intval = gstate.exitstatus;
        return itoa(intval);
    }
    if (*key == '$')
        return itoa((int)getpid());
    value = getenv(key);
    if (!value)
        value = strdup("");
    else
        value = strdup(value);
    assert(value);
    return value;
}

void    lexer_env_expand(lexlist_t *lexlist)
{
    char        *new;
    lexeme_t    *cur;

    cur = lexlist->head;
    while (cur)
    {
        if (cur->type == ENV && cur->qstate != IN_SQUOTE)
        {
            new = getenv_withexit(cur->content + 1); /* skip $ */
            free(cur->content);
            cur->content = new;
            cur->len = strlen(cur->content);
            cur->expanded = 1;
            cur->type = WORD;
        }
        cur = cur->next;
    }
}

static int quote_cond(lexeme_t *lex)
{
    return (lex->type & (SQUOTE | DQUOTE)) && lex->qstate == NOQUOTE;
}

static int whitespace_cond(lexeme_t *lex)
{
    return lex->type == WHITESPACE;
}

static int expanded_empty_cond(lexeme_t *lex)
{
    return lex->expanded && !lex->len;
}

/* merge right lexeme node into left node, assuming they're both not null and
   sets the type of the merged node as WORD */
static void merge_lexemes(lexeme_t *left, lexeme_t *right)
{
    left->len += right->len;
    left->content = realloc(left->content, left->len + 1);
    assert(left->content != NULL);
    strncat(left->content, right->content, right->len + 1);
    left->next = right->next;
    if (right->next)
        right->next->prev = left;
    left->type = WORD;
    free(right->content);
    free(right);
}

static void lexer_merge_adjacent_words(lexlist_t *list)
{
    lexeme_t *cur;

    cur = list->head;
    while (cur)
    {
        if (cur->type == WORD && cur->next && cur->next->type == WORD)
        {
            if (cur->next == list->tail)
                list->tail = cur;
            merge_lexemes(cur, cur->next);
        }
        else
            cur = cur->next;
    }
}

static void lexer_remove_lexemes(lexlist_t *list, int (*rm_cond)(lexeme_t *))
{
    lexeme_t    *cur, *temp;

    cur = list->head;
    while (cur)
    {
        temp = NULL;
        if ((*rm_cond)(cur))
        {
            temp = cur;
            if (cur == list->head)
                list->head = cur->next;
            if (cur == list->tail)
                list->tail = cur->prev;
            if (cur->next)
                cur->next->prev = cur->prev;
            if (cur->prev)
                cur->prev->next = cur->next;
            free(cur->content);
        }
        cur = cur->next;
        free(temp);
    }
}

/* j is the 2nd quote in pair of adjacent (empty) quotes */
static void handle_empty(lexeme_t *j)
{
    lexeme_t    *new;
    qstate_t    state;

    if (j->type == SQUOTE)
        state = IN_SQUOTE;
    else
        state = IN_DQUOTE;
    /* insert an empty string node between the quotes */
    new = new_lexeme("", 0, WORD, &state);
    new->next = j;
    new->prev = j->prev;
    if (j->prev)
        j->prev->next = new;
    j->prev = new;
}

/* Join all lexemes inside quotes into a single WORD lexeme */
/* Also check here that all quotes are closed */
static int lexer_merge_quotes(lexlist_t *list)
{
    lexeme_t    *i, *j;
    int         inside;

    i = list->head;
    if (!i)
        return EXIT_SUCCESS;
    inside = 0;
    while (i->next)
    {
        if (i->qstate == NOQUOTE && i->type & (SQUOTE | DQUOTE))
        {
            inside = !inside;
            if (inside)
            {
                j = i->next;
                if (j->qstate != NOQUOTE)
                    while (j->next && j->next->qstate != NOQUOTE)
                        merge_lexemes(j, j->next);
                else
                    handle_empty(j);
            }
        }
        else if (i->qstate != NOQUOTE)
            i->type = WORD;
        i = i->next;
    }
    if ((i->type & (SQUOTE | DQUOTE)) && i->qstate == NOQUOTE)
        inside = !inside;
    if (inside)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

lexlist_t   *lexer_simplify(lexlist_t *list)
{
    lexer_env_expand(list);
    lexer_remove_lexemes(list, &expanded_empty_cond);
    if (lexer_merge_quotes(list))
    {
        printerr("expected closing quote");
        lexlist_free(list);
        return NULL;
    }
    lexer_remove_lexemes(list, &quote_cond);
    lexer_merge_adjacent_words(list);
    lexer_remove_lexemes(list, &whitespace_cond);
    return list;
}


void    debug_lexlist(lexlist_t *list)
{
    lexeme_t   *cur;

    if (!list)
        return;
    fputs("\033[4mTYPE       | CONTENT              | EXP | LEN | QSTATE\n"
          "\033[0m", stderr);
    for (cur = list->head; cur; cur = cur->next)
    {
        switch (cur->type)
        {
            case WHITESPACE:
                fputs("WHITESPACE", stderr); break;
            case WORD:
                fputs("WORD      ", stderr); break;
            case SQUOTE:
                fputs("SQUOTE    ", stderr); break;
            case DQUOTE:
                fputs("DQUOTE    ", stderr); break;
            case ENV:
                fputs("ENV       ", stderr); break;
            case PIPELINE:
                fputs("PIPELINE  ", stderr); break;
            case REDIR_IN:
                fputs("REDIR_IN  ", stderr); break;
            case REDIR_OUT:
                fputs("REDIR_OUT ", stderr); break;
            case HERE_DOC:
                fputs("HERE_DOC  ", stderr); break;
            case REDIR_APP:
                fputs("REDIR_APP ", stderr); break;
        }
        fprintf(stderr, " | %-20s |  %d  | %3d | ",
            cur->content,
            cur->expanded,
            cur->len);
        switch (cur->qstate)
        {
            case NOQUOTE:
                fputs("NOQUOTE\n", stderr); break;
            case IN_DQUOTE:
                fputs("IN_DQUOTE\n", stderr); break;
            case IN_SQUOTE:
                fputs("IN_SQUOTE\n", stderr); break;
        }
    }
    fputs("\n", stderr);
}

void    debug_lexlist_tail(lexlist_t *list)
{
    lexeme_t   *cur;

    if (!list)
        return;
    fputs("\033[4mTYPE       | CONTENT              | EXP | LEN | QSTATE\n"
          "\033[0m", stderr);
    for (cur = list->tail; cur; cur = cur->prev)
    {
        switch (cur->type)
        {
            case WHITESPACE:
                fputs("WHITESPACE", stderr); break;
            case WORD:
                fputs("WORD      ", stderr); break;
            case SQUOTE:
                fputs("SQUOTE    ", stderr); break;
            case DQUOTE:
                fputs("DQUOTE    ", stderr); break;
            case ENV:
                fputs("ENV       ", stderr); break;
            case PIPELINE:
                fputs("PIPELINE  ", stderr); break;
            case REDIR_IN:
                fputs("REDIR_IN  ", stderr); break;
            case REDIR_OUT:
                fputs("REDIR_OUT ", stderr); break;
            case HERE_DOC:
                fputs("HERE_DOC  ", stderr); break;
            case REDIR_APP:
                fputs("REDIR_APP ", stderr); break;
        }
        fprintf(stderr, " | %-20s |  %d  | %3d | ",
            cur->content,
            cur->expanded,
            cur->len);
        switch (cur->qstate)
        {
            case NOQUOTE:
                fputs("NOQUOTE\n", stderr); break;
            case IN_DQUOTE:
                fputs("IN_DQUOTE\n", stderr); break;
            case IN_SQUOTE:
                fputs("IN_SQUOTE\n", stderr); break;
        }
    }
    fputs("\n", stderr);
}
