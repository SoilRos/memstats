volatile void * do_not_optimize;

int main() {
    // Allocate array of arrays
    int** ptr = new int*[5];
    do_not_optimize = ptr;
    for (int i = 0; i < 5; i++) {
        ptr[i] = new int[20];
        do_not_optimize = ptr[i];
    }

    ptr[1][0] = 42;

    for (int i = 0; i < 5; i++) {
        delete[] ptr[i];
    }
    delete[] ptr;

    return 0;
}