#include <stdlib.h>

struct fpstruct {
    int (*t_fptr)(int, int);
};

int plus(int a, int b) {
    return a + b;
}

int minus(int a, int b) {
    return a - b;
}

void foo(struct fpstruct *t, int (*a_fptr)(int, int)) {
    t->t_fptr = a_fptr;
}

int clever(int x) {
    int (*a_fptr)(int, int) = plus;
    int (*s_fptr)(int, int) = minus;
    struct fpstruct *t1 = malloc(sizeof(struct fpstruct));
    foo(t1, a_fptr);
    unsigned result = t1->t_fptr(1, 2);
    foo(t1, s_fptr);
    result = t1->t_fptr(1, 2);
    return 0;
}