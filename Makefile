######################### Preamble ###########################################
SHELL := bash
.ONESHELL:
.SHELLFLAGS := -eu -o pipefail -c
.DELETE_ON_ERROR:
.SECONDEXPANSION:
MAKEFLAGS += --warn-undefined-variables
MAKEFLAGS += --no-builtin-rules

######################### Project Settings ###################################
.PHONY: all
all: debug

NAME = app
BUILD_DIR := ./build
BIN_TARGET= $(BUILD_DIR)/$(NAME)
LIB_TARGET= $(BUILD_DIR)/lib$(NAME).so

SRC :=$(shell find . -name '*.c')
LIB_SRC :=$(shell find . -name '*.c' -not -name $(NAME).c)
OBJ :=$(SRC:%.c=$(BUILD_DIR)/%.o)
LIB_OBJ :=$(LIB_SRC:%.c=$(BUILD_DIR)/%.o)
DEP :=$(OBJ:.o=.d)
LIB :=$(addprefix -l,m)

WARN = -Wall -Wextra -Wvla -Wno-unused-parameter -Wno-unused-function
SANZ += -fno-omit-frame-pointer -fsanitize-trap=unreachable -fsanitize=undefined#,address

ifeq ($(CC),clang)
	CPPFLAGS += -Og -I./include -include-pch ./include/datatype99.h.pch
endif
CPPFLAGS += -I./include -I../../github/datatype99/build/_deps/metalang99-src/include/
CFLAGS   += -MMD -MP $(WARN)
LDFLAGS  += $(LIB)

.PHONY: debug release
debug: CFLAGS += $(SANZ) -Og -g3 -DLOGGING -DOOM_TRAP -DOOM_COMMIT
debug: LDFLAGS += $(SANZ)
debug: $(BIN_TARGET)

release: CFLAGS  += -O2 -g -DNDEBUG -DOOM_COMMIT
release: LDFLAGS +=
release: $(BIN_TARGET)

$(BIN_TARGET): $(OBJ)
	$(CC) -o $@ $(LDFLAGS) $^

$(LIB_TARGET): $(LIB_OBJ)
	$(CC) -o $@ -fPIC -shared $(LDFLAGS) $^

$(BUILD_DIR)/%.o : %.c
	mkdir -p $(dir $@)
	$(CC) -o $@ $(CPPFLAGS) $(CFLAGS) -c $<

-include $(DEP)

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

.PHONY: deps
deps:
	(cd include; ../pkg.sh import)
	curl -s --output-dir include -O https://raw.githubusercontent.com/JacksonAllan/CC/refs/heads/main/cc.h
	curl -s --output-dir include -O https://raw.githubusercontent.com/JacksonAllan/Verstable/refs/heads/main/verstable.h
	curl -s --output-dir include -O https://raw.githubusercontent.com/hirrolot/datatype99/refs/heads/master/datatype99.h
	curl -s --output-dir include -O https://raw.githubusercontent.com/spevnev/uprintf/main/uprintf.h

.PHONY: watch
watch:
	find . -name '*.c' -o -name '*.h' | entr -cc clang $(WARN) $(CPPFLAGS) -fsyntax-only -ferror-limit=1 -fmacro-backtrace-limit=1 /_
