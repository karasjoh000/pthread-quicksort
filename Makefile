sort: sort.o main.o
	gcc sort.o main.o -std=c99 -lpthread -o sort
main.o: sort.o
	gcc main.c -c -std=c99 -lpthread
sort.o:
	gcc sort.c -c -std=c99 -lpthread
clean:
	rm *.o sort sort2
sort2: sort2.o main2.o
	gcc sort2.o main2.o -std=c99 -lpthread -o sort2
main2.o: sort2.o
	gcc main2.c -c -std=c99 -lpthread
sort2.o:
	gcc sort2.c -c -std=c99 -lpthread
