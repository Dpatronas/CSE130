TARGET = httpserver

CC = gcc -pthread
CFLAGS = -Wall -Wextra -Werror -Wpedantic -std=c99 -g 

OBJ:= $(patsubst %.c,%.o,$(wildcard *.c))

all: $(TARGET)

$(TARGET): $(OBJ)

	$(CC) -o $@ ${OBJ}

%.o: %.c

	$(CC) $(CFLAGS) -o $@ -c $<

valgrind:
	valgrind ./$(TARGET) -A

clean:
	rm -f $(TARGET) *.o
