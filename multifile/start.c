/*
* start.c - Illustration of multi filed modules
*/
#include <linux/kernel.h>/* We’re doing kernel work */
#include <linux/module.h>/* Specifically, a module */

static int __init hello_module_init(void) {
    printk(KERN_INFO "Hello, world - this is the kernel speaking\n");
    return 0;
}

module_init(hello_module_init);

MODULE_AUTHOR("Eddie Davis <eddiedavis@boisestate.edu>");
MODULE_DESCRIPTION("Hello World Kernel Module");
MODULE_LICENSE("GPL v2");

