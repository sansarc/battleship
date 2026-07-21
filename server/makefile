CC = gcc
CFLAGS = -Wall -Wextra -pthread -I./include
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
EXECUTABLE = $(BIN_DIR)/server

all: directories $(EXECUTABLE)

directories:
		@mkdir -p $(OBJ_DIR)
		@mkdir -p $(BIN_DIR)

$(EXECUTABLE): $(OBJECTS)
		$(CC) $(CFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
		$(CC) $(CFLAGS) -c $< -o $@

clean:
		rm -rf $(OBJ_DIR) $(BIN_DIR)

run: all
		./$(EXECUTABLE)