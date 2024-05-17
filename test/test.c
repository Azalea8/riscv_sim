void fun(int *x);

int main() {
    int x = 1;
    fun(&x);
    return 0;
}

void fun(int *x) {
    *x += 10;
    return;
}