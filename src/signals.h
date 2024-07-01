#ifndef SIGNALS_H
#define SIGNALS_H

typedef enum
{
    NO_MODE,
    INTERACTIVE_MODE,
    EXECUTING_MODE,
    HEREDOC_MODE,
    INPIPE_MODE,
} signal_mode_t;

void    signals_check_exit(int, int);
void    handle_signals(signal_mode_t);

#endif
