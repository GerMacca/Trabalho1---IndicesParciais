CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -O2 -D_FILE_OFFSET_BITS=64
EXE     = ecommerce

# main.c inclui os outros .c, então só ele é compilado
$(EXE): src/*.c
	$(CC) $(CFLAGS) -o $@ src/main.c

clean:
	rm -f $(EXE) $(EXE).exe

.PHONY: clean
