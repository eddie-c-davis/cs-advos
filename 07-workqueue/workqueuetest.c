#include <linux/init.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
MODULE_LICENSE("GPL");

struct workqueue_struct *wq;
struct work_data {
    struct work_struct work;
    int data;
};

static void work_handler(struct work_struct *work) {
    struct work_data * data = (struct work_data *)work;
    printk(KERN_ALERT "workqueue_test: %s %d\n", __FUNCTION__, __LINE__);
    kfree(data);
}

static int wq_init(void) {
    struct work_data * data;
    printk(KERN_ALERT "workqueue_test: %s %d\n", __FUNCTION__, __LINE__);
    //wq = create_workqueue("wq_test");
    wq = create_singlethread_workqueue("wq_test");
    //wq = system_freezable_wq;
    //wq = system_wq;
    data = kmalloc(sizeof(struct work_data), GFP_KERNEL);
    INIT_WORK(&data->work, work_handler);
    queue_work(wq, &data->work);
    return 0;
}

static void wq_exit(void) {
    printk(KERN_ALERT "workqueue_test: %s %d\n", __FUNCTION__, __LINE__);
    flush_workqueue(wq);
    destroy_workqueue(wq);
    printk(KERN_ALERT "workqueue_test: %s %d\n", __FUNCTION__, __LINE__);
}

module_init(wq_init);
module_exit(wq_exit);
