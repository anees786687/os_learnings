CC = gcc
CFLAGS = -Wall -Wextra

# Every .c file here and one directory down is built into its own executable
SOURCES = $(wildcard *.c */*.c)
PROGRAMS = $(SOURCES:.c=)

all: $(PROGRAMS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROGRAMS)

.PHONY: all clean
