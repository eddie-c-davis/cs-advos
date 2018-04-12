#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>

struct file *kernel_fopen(const char *path, int flags, int rights);
void kernel_fclose(struct file *file);
int kernel_fread(struct file *file, unsigned long long offset, unsigned char *data, unsigned int size);
int kernel_fwrite(struct file *file, unsigned long long offset, unsigned char *data, unsigned int size);
int kernel_fsync(struct file *file);

