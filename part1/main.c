#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sort.h"

#define SIZE 3251

void getbuffer(char **array, int size) {
	for ( int i = 0; i < size; i++ )
		array[i] = (char*) malloc(sizeof(char)*500);
	return;

}

int main ( int argc, char** argv ) {

	char *buffer[SIZE];
	getbuffer(buffer, SIZE);
	char unparsed[500];

	FILE *fp = fopen(argv[1], "r+");

	int count = 0;


	while (count < SIZE && fgets(unparsed, 500, fp) > 0) {
		sscanf(unparsed, "%s\n", buffer[count]);
		count++;
	}

	fclose(fp);


	setSortThreads(30);
	sortThreaded( buffer, count );

	for (int i = 0; i < SIZE; i++ ) {
		printf("%s\n", buffer[i]);
	}


}
