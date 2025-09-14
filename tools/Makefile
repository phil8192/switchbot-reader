# Makefile for the simple ncurses SwitchBot dashboard
# - Builds console_simple_ncurses
# - Uses pkg-config for jansson + ncursesw if available, with sensible fallbacks
# - Provides `run` to pipe switchbot.py JSON into the dashboard
# - Configure average freshness window via STALE (defaults to 900s)

# ---- config ----
CC      ?= gcc
CSTD    ?= -std=c11
CFLAGS  ?= -O2 -Wall -Wextra $(CSTD)
LDFLAGS ?=
LDLIBS  ?= -lpthread -lm

# Try to get flags from pkg-config (if installed)
JANSSON_CFLAGS := $(shell pkg-config --cflags jansson 2>/dev/null)
JANSSON_LIBS   := $(shell pkg-config --libs   jansson 2>/dev/null)
NCURSES_CFLAGS := $(shell pkg-config --cflags ncursesw 2>/dev/null)
NCURSES_LIBS   := $(shell pkg-config --libs   ncursesw 2>/dev/null)

CFLAGS  += $(JANSSON_CFLAGS) $(NCURSES_CFLAGS)
LDLIBS  += $(JANSSON_LIBS) $(NCURSES_LIBS)

# Fallbacks if pkg-config didn’t provide anything
ifeq ($(strip $(JANSSON_LIBS)),)
  LDLIBS += -ljansson
endif
ifeq ($(strip $(NCURSES_LIBS)),)
  LDLIBS += -lncursesw
endif

# ---- targets ----
TARGET := ai_generated_console_dash_ncurses 
SRCS   := ai_generated_console_dash_ncurses.c
OBJS   := $(SRCS:.c=.o)

.PHONY: all clean run debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Quick debug build (no optimizations + symbols)
debug: CFLAGS := -O0 -g -Wall -Wextra $(CSTD) $(JANSSON_CFLAGS) $(NCURSES_CFLAGS)
debug: clean $(TARGET)

clean:
	$(RM) $(OBJS) $(TARGET)

# ---- run helper ----
# Usage examples:
#   make run
#   make run STALE=1800
#   make run PY=python3 SWITCHBOT_ARGS="-a -o json"
PY             ?= python
SWITCHBOT_ARGS ?= -a -o json
STALE          ?= 900

run: $(TARGET)
	@echo "Running: SB_STALE_SECS=$(STALE) $(PY) -u switchbot.py $(SWITCHBOT_ARGS) | ./$(TARGET)"
	@SB_STALE_SECS=$(STALE) $(PY) -u switchbot.py $(SWITCHBOT_ARGS) | ./$(TARGET)

