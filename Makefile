CC     = gcc
CFLAGS = -Wall -Wextra -g -Iinclude
BUILD := build

# make DEBUG=1      -> 編譯期啟用 LOG_DEBUG，輸出到 build-debug/
# make NO_ROBUST=1  -> 關閉強健性機制，輸出到 build-norobust/
ifdef DEBUG
CFLAGS += -DDEBUG
BUILD  := $(BUILD)-debug
endif
ifdef NO_ROBUST
CFLAGS += -DNO_ROBUST
BUILD  := $(BUILD)-norobust
endif

LIB     = $(BUILD)/libcommon.so
LDFLAGS = -L$(BUILD) -lcommon -Wl,-rpath,'$$ORIGIN'

all: $(LIB) $(BUILD)/server $(BUILD)/client

$(BUILD):
	mkdir -p $@

$(LIB): src/lib/common.c include/common.h | $(BUILD)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ src/lib/common.c

$(BUILD)/server: src/server/server.c include/common.h $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ src/server/server.c $(LDFLAGS)

$(BUILD)/client: src/client/client.c include/common.h $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ src/client/client.c $(LDFLAGS)

clean:
	rm -rf build build-debug build-norobust build-debug-norobust

.PHONY: all clean