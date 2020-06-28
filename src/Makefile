sort: sort.o main.o
	gcc sort.o main.o -g -std=c99 -lpthread -o sort
main.o: sort.o
	gcc main.c -c -g -std=c99 -lpthread
sort.o:
	gcc sort.c -c -g -std=c99 -lpthread
clean:
	rm *.o sort
