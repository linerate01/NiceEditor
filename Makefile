CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lncurses

TARGET = editor

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c $(LDFLAGS)

clean:
	rm -f $(TARGET)
