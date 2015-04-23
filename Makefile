CC=gcc
CFLAGS=-I. -Werror
LDFLAGS=-lX11 -lXdamage
OBJ = x-viredero.o
TEST_OBJ = xv-test-client.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

x-viredero: $(OBJ)
	gcc -o $@ $^ $(LDFLAGS)

xv-test-client: $(TEST_OBJ)
	gcc -o $@ $^ 

clean:
	$(RM) $(OBJ) x-viredero
