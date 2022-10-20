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

void dswap(char **a, char **b)
{
   swap(*a, *b);
}

int main() {
   char* a; 
   char **pa;
   char* b;
   char **pb;
   a = (char *)MALLOC(1);
   b = (char *)MALLOC(1);
   pa = (char **)MALLOC(8);
   pb = (char **)MALLOC(8);
   
   *b = 24;
   *a = 42;

   *pa = a;
   *pb = b;

   dswap(pa, pb);

   PRINT((int)*a);
   PRINT((int)*b);
   FREE(a);
   FREE(b);
   return 0;
}

