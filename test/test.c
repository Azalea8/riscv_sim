int fun(int x);

int main(int argc, char **argv) {
    int x = (int)argv[0];
    int ans = fun(x);
    return 0;
}

int fun(int x) {
    if(x == 0) {
        return 1;
    }
    if(x == 1) {
        return 1;
    }
    return fun(x - 1) + fun(x - 2);
}