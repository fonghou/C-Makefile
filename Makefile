EXE = main
BUILD_DIR := ./build
TARGET= $(BUILD_DIR)/$(EXE)

SRC :=$(shell find . -name '*.c')
OBJ :=$(SRC:%.c=$(BUILD_DIR)/%.o)
DEP :=$(OBJS:.o=.d)
LIB :=$(addprefix -l,m)

WARN = -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
SANZ += -fsanitize=undefined -fsanitize-trap=unreachable

CPPFLAGS += -MMD -MP -Ideps
CFLAGS   += -ggdb3 -fno-omit-frame-pointer $(SANZ) $(WARN)
LDFLAGS  += $(LIB) $(SANZ)

.PHONY: all
all: debug

.PHONY: deps
deps:
	(cd deps; pkg.sh import)
	curl --output-dir deps -O https://raw.githubusercontent.com/ashvardanian/StringZilla/main/include/stringzilla/stringzilla.h
	curl --output-dir deps -O https://raw.githubusercontent.com/spevnev/uprintf/main/uprintf.h

.PHONY: watch
watch:
	find -name '*.c' -o -name '*.h' | entr -cc clang $(WARN) -fsyntax-only -ferror-limit=1 -fmacro-backtrace-limit=1 /_

.PHONY: debug release
debug: CFLAGS += -Og
debug: $(TARGET)

release: CFLAGS += -O3 -DNDEBUG
release: LDFLAGS += -static-libgcc -static-libubsan
release: $(TARGET)

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o : %.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

-include $(DEPS)
