#include "lib.h" 

int main(void) {
    int initial_value = 5;

    print_s("Prog1: Outputting initial value: ");
    print_d(initial_value);
    print_s("\n\r");

    set_shared_mem(initial_value);
    return 0;
}