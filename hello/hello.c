#include <stdio.h>
#include <string.h>

// Emscripten functionality
#include <emscripten.h>

EMSCRIPTEN_KEEPALIVE double add(double a, double b){
	return a+b;
}

EMSCRIPTEN_KEEPALIVE const char * greet(const char* _name){
	char rv[] = "Hello, ";
	char buf[256];
	strcpy(buf, _name);
	strcat(rv, buf);
	strcat(rv, "!");
	return rv;
}
