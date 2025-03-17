volatile void * do_not_optimize;

int main() {
    int* ptr = new int;
    do_not_optimize = ptr;
    delete ptr;
    delete ptr;

    return 0;
}