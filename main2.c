#include <stdio.h>
#include "sort.h"

int main ( int argc, char** argv ) {
	setSortThreads(30);
	sortThreaded( &argv[1], argc-1 );
	for (int i = 1; i < argc; i++ ) {
		printf("%s\n", argv[i]);
	}

}
