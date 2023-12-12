CC=gcc
CFLAGS=-I.
DEPS =
OBJ = fsx.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

fsx: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)
