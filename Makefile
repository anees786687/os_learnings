CC = gcc
CFLAGS = -Wall -Wextra

# Every .c file here and up to two directories down is built into its own executable
SOURCES = $(wildcard *.c */*.c */*/*.c)
PROGRAMS = $(SOURCES:.c=)

all: $(PROGRAMS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROGRAMS)

.PHONY: all clean
