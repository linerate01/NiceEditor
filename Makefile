CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lncurses -lpthread

TARGET = editor

all: $(TARGET)

$(TARGET): editor.c
        $(CC) $(CFLAGS) -o $(TARGET) editor.c $(LDFLAGS)

clean:
        rm -f $(TARGET)