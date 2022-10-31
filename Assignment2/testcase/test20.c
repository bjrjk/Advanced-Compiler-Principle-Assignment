#include <stdio.h>

int add(int a, int b) {
    return a + b;
}

int sub(int a, int b) {
    return a - b;
}

int foo(int a, int b, int (*a_fptr)(int, int)) {
    return a_fptr(a, b);
}


int main() {
    int (*a_fptr)(int, int) = add;
    int (*s_fptr)(int, int) = sub;
    int (*t_fptr)(int, int) = 0;

    char x;
    int op1, op2;
    fprintf(stderr, "Please input num1 +/- num2 \n");

    fscanf(stdin, "%d %c %d", &op1, &x, &op2);

    if (x == '+') {
        t_fptr = a_fptr;
    }

    if (x == '-') {
        t_fptr = s_fptr;
    }

    if (t_fptr != NULL) {
        unsigned result = foo(op1, op2, t_fptr);
        fprintf(stderr, "Result is %d \n", result);
    } else {
        fprintf(stderr, "Unrecoganised operation %c", x);
    }
    return 0;
}