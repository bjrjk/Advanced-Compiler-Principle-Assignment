#include <stdio.h>
#include <stdlib.h>

int GET() {
    int v;
    scanf("%d", &v);
    return v;
}
void * MALLOC(int size) {
    return malloc(size);
}
void FREE(void *addr) {
    free(addr);
}
void PRINT(int v) {
    printf("%d", v);
}

