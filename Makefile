CC = gcc
FLAG = -ggdb3
THREAD = -lpthread
OBJ = proxy.o csapp.o

proxy: proxy.o csapp.o
	$(CC) $(FLAG) $(OBJ) -o proxy $(THREAD)

proxy.o: proxy.c
	$(CC) $(FLAG) -c proxy.c 

csapp.o: csapp.c csapp.h
	$(CC) $(FLAG) -c csapp.c

clean:
	rm -f $(OBJ) proxy
