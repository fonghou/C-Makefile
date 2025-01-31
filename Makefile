EXE = main
BUILD_DIR := ./build
TARGET= $(BUILD_DIR)/$(EXE)

SRC :=$(shell find . -name '*.c' | grep -v STC)
OBJ :=$(SRC:%.c=$(BUILD_DIR)/%.o)
DEP :=$(OBJS:.o=.d)
LIB :=$(addprefix -l,stc)

WARN = -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-deprecated-declarations
SANZ += -fno-omit-frame-pointer -fno-common -fsanitize-trap=unreachable -fsanitize=undefined#,address

CPPFLAGS += -I./include -I./STC/include
CFLAGS   += -MMD -MP $(WARN)
LDFLAGS  += -L./STC/build $(LIB)

.PHONY: all
all: debug

.PHONY: deps
deps:
	(cd include; ../pkg.sh import)
	git submodule update --init --remote --recursive
	curl -s --output-dir include -O https://raw.githubusercontent.com/spevnev/uprintf/main/uprintf.h

.PHONY: watch
watch:
	find . -name '*.c' -o -name '*.h' | entr -cc clang $(CPPFLAGS) $(WARN) -fsyntax-only -ferror-limit=1 -fmacro-backtrace-limit=1 /_

.PHONY: debug release
debug: CFLAGS += $(SANZ) -O0 -g3 -DLOGGING -DOOM
debug: LDFLAGS += $(SANZ)
debug: $(TARGET)

release: CFLAGS  += -O3 -g -DNDEBUG
release: LDFLAGS += #-static-libgcc
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
