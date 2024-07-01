#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "icshell.h"
#include "lexer.h"
#include "builtins.h"
#include "parse.h"
#include "execution.h"
#include "signals.h"
#include "asciiart.h"

gstate_t    gstate;

static void process(char *line)
{
    lexlist_t   *lexlist;
    parsenode_t *parsetree;

    lexlist = lexer_create(line);
    lexlist = lexer_simplify(lexlist);
    handle_signals(NO_MODE);
    if (!lexlist || !lexlist->head)
    {
        free(lexlist);
        return;
    }
    if (!builtins_handle(lexlist->head))
    {
        if (fork_and_check() == 0) /* Child process */
        {
            handle_signals(EXECUTING_MODE);
            parsetree = parse_create(lexlist);
            execute_node(parsetree, 0);
        }
        wait(&gstate.exitstatus);
        signals_check_exit(gstate.exitstatus, 1); /* print newline as well */
    }
    lexlist_free(lexlist);
}

int run_from_file(char *filename)
{
    FILE    *file;
    char    buf[BUFSIZ], *newline;

    file = fopen(filename, "r");
    if (!file)
        perror_exit(filename, EXIT_FAILURE);
    /* Do not use an internal buffer because our program messes it up
     * for some reason. If you remove this line the file will infinitely
     * read and the program never terminates. */
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(file, NULL, _IONBF, 0);
    while (fgets(buf, sizeof(buf), file))
    {

        newline = strchr(buf, '\n');
        if (newline)
            *newline = '\0';
        process(buf);
    }
    if (ferror(file))
        perror_exit(filename, EXIT_FAILURE);
    fclose(file);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    char    *command_line, *prompt;

    setup_env(argc, argv);
    if (argc == 3 && !strcmp("-c", argv[1]))
        return run_from_file(argv[2]);
    fputs(WELCOME_MESSAGE, stdout);
    while (1)
    {
        handle_signals(INTERACTIVE_MODE);
        prompt = current_dir_prompt();
        command_line = readline(prompt);
        free(prompt);
        if (command_line && *command_line)
            add_history(command_line);
        rl_on_new_line();
        handle_signals(EXECUTING_MODE);
        if (!command_line)
            break;
        if (command_line && *command_line)
            process(command_line);
        free(command_line);
    }
    return EXIT_SUCCESS;
}
