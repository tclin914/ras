all: ras

ras: ras.c sem.o
	gcc -g $< -o $@ sem.o

sem.o: sem.c
	gcc $< -c -o $@

clean: 
	rm ras
	rm sem.o
