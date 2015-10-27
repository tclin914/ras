../ras: ras.c
	gcc -g $< -o $@

clean: 
	rm ../ras
