#ifndef LEXER_H
#define LEXER_H

#include <stdint.h>

typedef enum
{
    WHITESPACE  = (1 << 0),
    WORD        = (1 << 1), /* word:     anything   */
    SQUOTE      = (1 << 2), /* squote:   '          */
    DQUOTE      = (1 << 3), /* dquote:   "          */
    ENV         = (1 << 4), /* env:      $something */
    PIPELINE    = (1 << 5), /* pipe:     |          */
    REDIR_IN    = (1 << 6), /* in:       <          */
    REDIR_OUT   = (1 << 7), /* out:      >          */
    HERE_DOC    = (1 << 8), /* here-doc: <<         */
    REDIR_APP   = (1 << 9)  /* append:   >>         */
} lextype_t;

typedef enum
{
    NOQUOTE,
    IN_SQUOTE,
    IN_DQUOTE
} qstate_t;

typedef struct lexeme_s
{
    struct lexeme_s *next;      /* next lexer node */
    struct lexeme_s *prev;      /* previous lexer node */
    char            *content;   /* the actual text entered in the token */
    lextype_t       type;       /* the type of the lexer node */
    uint32_t        len;        /* the length of content */
    uint8_t         expanded;   /* whether the environment variable in the
                                   node has been expanded */
    qstate_t        qstate;     /* the state of quotes at this lexer node */
} lexeme_t;

typedef struct
{
    lexeme_t    *head;
    lexeme_t    *tail;
} lexlist_t;

lexeme_t    *new_lexeme(char *, uint32_t, lextype_t, qstate_t *);
lexlist_t   *lexer_create(char *);
void        lexlist_free(lexlist_t *);
lexlist_t   *lexer_simplify(lexlist_t *);
void        lexer_env_expand(lexlist_t *);

/* DEBUG */
void        debug_lexlist(lexlist_t *);
void        debug_lexlist_tail(lexlist_t *);

#endif
