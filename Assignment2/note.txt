output format：

${line} : ${func_name1}, ${func_name2}


${line} is unique in output.


Given the test file:

  1 #include <stdio.h>
  2 
  3 int add(int a, int b) {
  4    return a+b;
  5 }
  6 
  7 int sub(int a, int b) {
  8    return a-b;
  9 }
 10 
 11 int foo(int a, int b, int (*a_fptr)(int, int)) {
 12     return a_fptr(a, b);
 13 }
 14 
 15 
 16 int main() {
 17     int (*a_fptr)(int, int) = add;
 18     int (*s_fptr)(int, int) = sub;
 19     int (*t_fptr)(int, int) = 0;
 20 
 21     char x;
 22     int op1, op2;
 23     fprintf(stderr, "Please input num1 +/- num2 \n");
 24 
 25     fscanf(stdin, "%d %c %d", &op1, &x, &op2);
 26 
 27     if (x == '+') {
 28        t_fptr = a_fptr;
 29     }
 30 
 31     if (x == '-') {
 32        t_fptr = s_fptr;
 33     }
 34 
 35     if (t_fptr != NULL) {
 36        unsigned result = foo(op1, op2, t_fptr);
 37        fprintf (stderr, "Result is %d \n", result);
 38     } else {
 39        fprintf (stderr, "Unrecoganised operation %c", x);
 40     }
 41     return 0;
 42 }

The output should be :

12 : add, sub
23 : fprintf
25 : fscanf
36 : foo
37 : fprintf
39 : fprintf
