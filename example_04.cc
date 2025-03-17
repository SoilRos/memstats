volatile void * do_not_optimize;

int main()
{
    auto val = new int;
    do_not_optimize = val;

    return 0;
}
