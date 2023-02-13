CC=	gcc
CFLAGS= -g -Wall

shell: shell.o
	$(CC) shell.o -o shell

shell.o: src/shell.c
	$(CC) $(CFLAGS) -c src/shell.c

run: shell
	./shell

clean:
	rm -rf *.o shell


