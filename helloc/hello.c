#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Emscripten functionality
//#include <emscripten.h>

// EMSCRIPTEN_KEEPALIVE
void sayHello(){
	printf("Hello, World!\n"); // js console reads until newline !
}

// EMSCRIPTEN_KEEPALIVE
double add(double a, double b){
	return a+b;
}

// EMSCRIPTEN_KEEPALIVE
char * greet(const char* _name){
	printf("_name: %s\n", _name);
	char* rv = (char *)malloc(256*sizeof(char));
	strcpy(rv, "Hello, ");
	char buf[256];
	strcpy(buf, _name);
	strcat(rv, buf);
	strcat(rv, "!");
	return rv;
}
