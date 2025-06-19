# Compiler and flags
CC = gcc
CFLAGS = 
LDFLAGS = -lws2_32

# Target executable
TARGET = mainserver.exe

# Source files
SRC = mainserver.c

# Build target
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

# Clean target
clean:
	del /Q *.exe *.o 2>nul || true
