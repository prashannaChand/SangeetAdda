# Path to PortAudio
PORTAUDIO_INC = ../portaudio/include
PORTAUDIO_LIB = ../portaudio/lib

# Compiler and flags
CC = gcc
CFLAGS = -I$(PORTAUDIO_INC)
LDFLAGS = -L$(PORTAUDIO_LIB) -lportaudio -lws2_32

# Target executable
TARGET = mainclient.exe

# Source files
SRC = mainclient.c

# Build target
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

# Clean target
clean:
	del /Q *.exe *.o 2>nul || true
