ssu_mntr : ssu_mntr.o
	gcc -o ssu_mntr ssu_mntr.o -lpthread

ssu_mntr.o : ssu_mntr.c
	gcc -c ssu_mntr.c -lpthread

clean :
	rm ssu_mntr.o
