all: ras

ras: ras.c sem.o fifo.o
	gcc -g $< -o $@ sem.o fifo.o

sem.o: sem.c
	gcc $< -c -o $@

fifo.o: fifo.c
	gcc $< -c -o $@

clean: 
	rm ras
	rm sem.o
	rm fifo.o
