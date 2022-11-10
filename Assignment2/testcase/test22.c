#include <stdlib.h>
int plus(int a, int b) {
   return a+b;
}

int minus(int a, int b) {
   return a-b;
}

int (*foo(int a, int b, int (*a_fptr)(int, int), int(*b_fptr)(int, int) ))(int, int) {
   return a_fptr;
}
int (*clever(int a, int b, int (*a_fptr)(int, int), int(*b_fptr)(int, int) ))(int, int) {
   return foo(a,b,a_fptr,b_fptr);
}
int (*clever1(int (* (*goo_ptr)(int, int, int (*)(int, int), int(*)(int, int)))(int, int), int a, int b, int (*a_fptr)(int, int), int(*b_fptr)(int, int) ))(int, int) {
   return goo_ptr(a,b,b_fptr,a_fptr);
}
int moo(char x, int op1, int op2) {
    int (*a_fptr)(int, int) = plus;
    int (*s_fptr)(int, int) = minus;
    int (* (*goo_ptr)(int, int, int (*)(int, int), int(*)(int, int)))(int, int)=clever;

    int (*t_fptr)(int, int) = clever1(goo_ptr, op1, op2, s_fptr, a_fptr); 
    t_fptr(op1, op2);
    
    return 0;
}

// 14 : foo
// 17 : clever
// 24 : clever1
// 25 : plus
