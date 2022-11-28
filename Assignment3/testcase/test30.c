int main() {
    int x, y;
    int *z, *a;
    z = &x;
    a = z;
    y = *a;
    *z = y;
}