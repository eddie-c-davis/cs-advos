/*
* hello2.c - The simplest kernel module.
*/
#include <linux/module.h>/* Needed by all modules */
#include <linux/kernel.h>/* Needed for KERN_INFO */

static int __init hello_module_init(void) {
    printk(KERN_INFO "Hello world 2.\n");
    return 0;
}

static void __exit hello_module_exit(void) {
    printk(KERN_INFO "Goodbye world 2.\n");
}

module_init(hello_module_init);
module_exit(hello_module_exit);

MODULE_AUTHOR("Eddie Davis <eddiedavis@boisestate.edu>");
MODULE_DESCRIPTION("Hello World Kernel Module");
MODULE_LICENSE("GPL v2");

