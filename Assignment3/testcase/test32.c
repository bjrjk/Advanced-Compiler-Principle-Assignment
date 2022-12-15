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

void foo(struct fpstruct *t, int flag) {
    if (flag == 3) {
        t->t_fptr = plus;
    } else {
        t->t_fptr = minus;
    }
}

int clever(int x) {
    int (*a_fptr)(int, int) = plus;
    int (*s_fptr)(int, int) = minus;
    int op1 = 1;
    struct fpstruct *t1 = malloc(sizeof(struct fpstruct));

    foo(t1, op1);
    unsigned result = t1->t_fptr(op1, 2);
    return 0;
}