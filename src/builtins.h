#ifndef BUILTINS_H
#define BUILTINS_H

#include "lexer.h"
#include "parse.h"

#define EXIT_INVALID_BUILTIN    2

int     builtins_handle(lexeme_t *);
void    builtins_infork(exec_t *);
int     set_pwd(char *);

#endif
