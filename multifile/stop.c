/*
* stop.c - Illustration of multi filed modules
*/
#include <linux/kernel.h>/* We’re doing kernel work */
#include <linux/module.h>/* Specifically, a module */

static void __exit hello_module_exit(void) {
    printk(KERN_INFO "Short is the life of a kernel module\n");
}

module_exit(hello_module_exit);

MODULE_AUTHOR("Eddie Davis <eddiedavis@boisestate.edu>");
MODULE_DESCRIPTION("Hello World Kernel Module");
MODULE_LICENSE("GPL v2");

