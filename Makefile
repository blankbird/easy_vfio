# easy_vfio - Makefile
#
# SPDX-License-Identifier: MIT

CC       ?= gcc
AR       ?= ar
CFLAGS   ?= -Wall -Wextra -Werror -O2
CFLAGS   += -Iinclude -fPIC

SRCDIR   = src
INCDIR   = include
TESTDIR  = tests
EXDIR    = examples

SRCS     = $(wildcard $(SRCDIR)/*.c)
OBJS     = $(SRCS:.c=.o)

LIB_STATIC = libeasy_vfio.a
LIB_SHARED = libeasy_vfio.so

TEST_SRC   = $(TESTDIR)/test_easy_vfio.c
TEST_BIN   = $(TESTDIR)/test_easy_vfio

EXAMPLE_SRC = $(EXDIR)/example_basic.c
EXAMPLE_BIN = $(EXDIR)/example_basic

.PHONY: all clean test examples install uninstall

all: $(LIB_STATIC) $(LIB_SHARED)

$(LIB_STATIC): $(OBJS)
	$(AR) rcs $@ $^

$(LIB_SHARED): $(OBJS)
	$(CC) -shared -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(LIB_STATIC) $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) $(LIB_STATIC)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_STATIC)

examples: $(LIB_STATIC) $(EXAMPLE_BIN)

$(EXAMPLE_BIN): $(EXAMPLE_SRC) $(LIB_STATIC)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_STATIC)

clean:
	rm -f $(OBJS) $(LIB_STATIC) $(LIB_SHARED)
	rm -f $(TEST_BIN) $(EXAMPLE_BIN)

PREFIX   ?= /usr/local
LIBDIR   ?= $(PREFIX)/lib
HDRDIR   ?= $(PREFIX)/include

install: $(LIB_STATIC) $(LIB_SHARED)
	install -d $(DESTDIR)$(LIBDIR) $(DESTDIR)$(HDRDIR)
	install -m 644 $(LIB_STATIC) $(DESTDIR)$(LIBDIR)/
	install -m 755 $(LIB_SHARED) $(DESTDIR)$(LIBDIR)/
	install -m 644 $(INCDIR)/easy_vfio.h $(DESTDIR)$(HDRDIR)/

uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_STATIC)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_SHARED)
	rm -f $(DESTDIR)$(HDRDIR)/easy_vfio.h
