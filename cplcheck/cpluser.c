#include <stdio.h>

int main() {
    unsigned int csr, mask, cpl;
    asm("movl %%cs,%0" : "=r" (csr));

    mask = ((1 << 2) - 1) << 0;
    cpl = csr & mask;

    printf("cplcheck: CSR: %04x, CPL: %d\n", csr, cpl);

    return 0;
}
