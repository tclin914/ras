../ras: ras.c
	gcc $< -o $@

clean: 
	rm ../ras
