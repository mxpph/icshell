#ifndef EXECUTION_H
#define EXECUTION_H

#include "parse.h"

#define ERROR_NOT_EXECUTABLE     126
#define ERROR_NOT_FOUND          127
#define WR_PERMS                 0644

void    execute_node(parsenode_t *, int);

#endif
