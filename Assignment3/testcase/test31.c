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

int clever(int x) {
    int (*a_fptr)(int, int) = plus;
    int (*s_fptr)(int, int) = minus;
    int op1 = 1, op2 = 2;
    struct fpstruct *t1 = malloc(sizeof(struct fpstruct));
    if (x == 3) {
        t1->t_fptr = a_fptr;
    } else {
        t1->t_fptr = s_fptr;
    }
    unsigned result = t1->t_fptr(op1, op2);
    return 0;
}