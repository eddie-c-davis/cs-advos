/*
* hello2.c - The simplest kernel module.
*/
#include <linux/module.h>/* Needed by all modules */
#include <linux/kernel.h>/* Needed for KERN_INFO */

//EXPORT_SYMBOL_GPL(show_regs);

static int __init cplcheck_init(void) {
    unsigned int csr, mask, cpl;
    asm("movl %%cs,%0" : "=r" (csr));

    mask = ((1 << 2) - 1) << 0;
    cpl = csr & mask;

    printk(KERN_INFO "cplcheck: CSR: %04x, CPL: %d\n", csr, cpl);

    return 0;
}

static void __exit cplcheck_exit(void) {
    printk(KERN_INFO "Unloaded cplkernel...\n");
}

module_init(cplcheck_init);
module_exit(cplcheck_exit);

MODULE_AUTHOR("Eddie Davis <eddiedavis@boisestate.edu>");
MODULE_DESCRIPTION("CPL Check Kernel Module");
MODULE_LICENSE("GPL v2");

