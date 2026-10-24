#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>

#include <linux/uaccess.h>
#include <linux/minmax.h>
#include <linux/errno.h>

#define MYDEVICE_BUF_SIZE       256

static char mydevice_buf[MYDEVICE_BUF_SIZE];
static size_t head;   // next write position
static size_t tail;   // next read position
static size_t used;   // used in buffer





static ssize_t mydevice_read(struct file *file, char __user *buf,
			       size_t count, loff_t *ppos)
{
	ssize_t ret;
	ssize_t first, second, copied;
	
	if(count == 0) return 0;
	
	if(used == 0) return -EAGAIN;
	
	count = min_t(size_t, count, used);
	first = min_t(size_t, count, MYDEVICE_BUF_SIZE - tail);
	second = count - first;
	
	ret = copy_to_user(buf, mydevice_buf + tail, first);
	copied = first - ret;
	
	if(copied == 0) return -EFAULT;
	
	if(copied != first) goto out;
	
	if(second > 0) {
		ret = copy_to_user(buf + first, mydevice_buf, second);
		copied = copied + second - ret;
		}
		
out:
	tail = (tail + copied) % MYDEVICE_BUF_SIZE;
	used -= copied;

	return copied ? copied : -EFAULT;
}

static ssize_t mydevice_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	ssize_t ret;
	ssize_t first, second, copied;
	
	if(count == 0) return 0;

	if (used >= MYDEVICE_BUF_SIZE) return -EAGAIN;

	count = min_t(size_t, count, MYDEVICE_BUF_SIZE - used);
	first = min_t(size_t, count, MYDEVICE_BUF_SIZE - head);
	second = count - first;
	
	ret = copy_from_user(mydevice_buf + head, buf, first);
	copied = first - ret;
	
	if(copied == 0) return -EFAULT;
	
	if(copied != first) goto out;
	
	if(second > 0) {
	  ret = copy_from_user(mydevice_buf, buf + first, second);
	  copied = copied + second - ret;
	  }
	  
out:
	head = (head + copied) % MYDEVICE_BUF_SIZE;
	used += copied;
	  
	return copied ? copied : -EFAULT;
}

static const struct file_operations mydevice_misc_fops = {
	//.owner		= THIS_MODULE,
	.read           = mydevice_read,
	.write          = mydevice_write,

};

static struct miscdevice mydevice_misc = {
	MISC_DYNAMIC_MINOR,
	"mydevice",
	&mydevice_misc_fops,
};

static int __init mydevice_module_init(void)
{
	int ret;

	ret = misc_register(&mydevice_misc);
	if (ret) {
		pr_err("mydevice: misc_register failed\n");
		return ret;
	}

	pr_info("mydevice: registered successfully\n");
	return 0;
}

static void __exit mydevice_module_exit(void)
{
	misc_deregister(&mydevice_misc);
	pr_info("mydevice: unregistered successfully\n");
}

module_init(mydevice_module_init);
module_exit(mydevice_module_exit);

MODULE_LICENSE("GPL");
