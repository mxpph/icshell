# Requires: libreadline8, libreadline-dev, libncurses-dev
# sudo apt-get install libreadline8 libreadline-dev libncurses-dev

CC      ?= gcc
CFLAGS  ?= -std=c17 -g\
	-D_POSIX_SOURCE -D_DEFAULT_SOURCE\
	-Wall -Werror -pedantic

.SUFFIXES: .c .o

.PHONY: all clean re

LIBS := -lreadline -lncurses
SRCS_DIR := ./src
SRCS := $(wildcard $(SRCS_DIR)/*.c)
OBJS := $(SRCS:.c=.o)
TESTDIR := tester/tests
TESTS := $(shell find $(TESTDIR) -type f -exec basename {} \;)

all: icshell

icshell: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

test: clean_test icshell
	cd tester && python3 test.py

clean_test:
	$(RM) -r $(addprefix tester/, $(TESTS)) tester/files_backup \
	tester/files/outfile

clean: clean_test
	$(RM) $(SRCS_DIR)/*.o icshell

re: clean all
