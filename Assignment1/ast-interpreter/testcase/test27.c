extern int GET();
extern void * MALLOC(int);
extern void FREE(void *);
extern void PRINT(int);


void swap(char *a, char *b) {
   char temp;
   temp = *a;
   *a = *b;
   *b = temp;
}

int main() {
   char* a; 
   char* b;
   a = (char *)MALLOC(1);
   b = (char *)MALLOC(1);
   
   *b = 24;
   *a = 42;

   swap(a, b);

   PRINT((int)*a);
   PRINT((int)*b);
   FREE(a);
   FREE(b);
   return 0;
}

