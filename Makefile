CC := gcc
CFLAGS := -Wall -Wextra -pedantic -std=c11 -pthread
LDLIBS :=
BUILD_DIR := build
SRC_DIR := src
TARGET_TRIPLE := $(shell $(CC) -dumpmachine 2>/dev/null)

ifneq (,$(findstring mingw,$(TARGET_TRIPLE)))
LDLIBS += -lws2_32
SERVER_BIN := $(BUILD_DIR)/servidor.exe
CLIENT_BIN := $(BUILD_DIR)/cliente.exe
GUI_BIN := $(BUILD_DIR)/socket_gui.exe
GUI_LDLIBS := -lws2_32 -mwindows
RM := rm -rf
MKDIR := mkdir -p
else
ifneq (,$(findstring cygwin,$(TARGET_TRIPLE)))
SERVER_BIN := $(BUILD_DIR)/servidor.exe
CLIENT_BIN := $(BUILD_DIR)/cliente.exe
else
SERVER_BIN := $(BUILD_DIR)/servidor
CLIENT_BIN := $(BUILD_DIR)/cliente
endif
RM := rm -rf
MKDIR := mkdir -p
endif

COMMON_SOURCES := $(SRC_DIR)/messaging.c
SERVER_SOURCES := $(SRC_DIR)/server.c $(COMMON_SOURCES)
CLIENT_SOURCES := $(SRC_DIR)/client.c $(COMMON_SOURCES)
GUI_SOURCES := $(SRC_DIR)/gui_app.c

.PHONY: all clean run-server run-client

ifneq (,$(findstring mingw,$(TARGET_TRIPLE)))
all: $(SERVER_BIN) $(CLIENT_BIN) $(GUI_BIN)
else
all: $(SERVER_BIN) $(CLIENT_BIN)
endif

$(BUILD_DIR):
	$(MKDIR) $(BUILD_DIR)

$(SERVER_BIN): $(SERVER_SOURCES) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SERVER_SOURCES) -o $(SERVER_BIN) $(LDLIBS)

$(CLIENT_BIN): $(CLIENT_SOURCES) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CLIENT_SOURCES) -o $(CLIENT_BIN) $(LDLIBS)

$(GUI_BIN): $(GUI_SOURCES) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(GUI_SOURCES) -o $(GUI_BIN) $(GUI_LDLIBS)

run-server: $(SERVER_BIN)
	./$(SERVER_BIN) 5000

run-client: $(CLIENT_BIN)
	./$(CLIENT_BIN) 127.0.0.1 5000

clean:
	$(RM) $(BUILD_DIR)
