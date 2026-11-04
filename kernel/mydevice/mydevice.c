#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>

#include <linux/uaccess.h>
#include <linux/minmax.h>
#include <linux/errno.h>

#include <linux/mutex.h>

#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>

#include <linux/slab.h>


#define MYDEVICE_BUF_SIZE       256

static char mydevice_buf[MYDEVICE_BUF_SIZE];
static size_t head;   // next write position
static size_t tail;   // next read position
static size_t used;   // used in buffer

static DEFINE_MUTEX(mydevice_mutex);

struct mydevice_data {
  struct gpio_desc *control_gpio;
  struct miscdevice misdev;
  };
  
  
static ssize_t mydevice_read(struct file *file, char __user *buf,
			       size_t count, loff_t *ppos)
{
	ssize_t ret;
	ssize_t first, second, copied;
	
	if(count == 0) return 0;
	
mutex_lock(&mydevice_mutex);
	
	if(used == 0) {
	mutex_unlock(&mydevice_mutex);
	return -EAGAIN;
	}
	
	count = min_t(size_t, count, used);
	first = min_t(size_t, count, MYDEVICE_BUF_SIZE - tail);
	second = count - first;
	
	ret = copy_to_user(buf, mydevice_buf + tail, first);
	copied = first - ret;
	
	if(copied == 0) {
	mutex_unlock(&mydevice_mutex);
	return -EFAULT;
	}
	
	if(copied != first) goto out;
	
	if(second > 0) {
		ret = copy_to_user(buf + first, mydevice_buf, second);
		copied = copied + second - ret;
		}
		
out:
	tail = (tail + copied) % MYDEVICE_BUF_SIZE;
	used -= copied;
	
mutex_unlock(&mydevice_mutex);

	return copied ? copied : -EFAULT;
}

static ssize_t mydevice_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	ssize_t ret;
	ssize_t first, second, copied;
	char cmd[6];
	
	if(count == 0) return 0;
	
	if(count == 6) {
	    struct miscdevice *misc = file->private_data;
	    struct mydevice_data *data = container_of(misc, struct mydevice_data, misdev);

	    if(copy_from_user(cmd, buf, 6))
		      return -EFAULT;

	    if(memcmp(cmd, "GPIO 1", 6) == 0) {
		  gpiod_set_value(data->control_gpio, 1);
		  
		  pr_info("GPIO 0 value: %d\n", gpiod_get_value(data->control_gpio));
		      return count;
		}

    	    if (memcmp(cmd, "GPIO 0", 6) == 0) {
        	  gpiod_set_value(data->control_gpio, 0);
        	  pr_info("GPIO 0 value: %d\n", gpiod_get_value(data->control_gpio));
        	      return count;
    		}
      }

	
mutex_lock(&mydevice_mutex);

	if (used >= MYDEVICE_BUF_SIZE) {
	mutex_unlock(&mydevice_mutex);
	return -EAGAIN;
	}

	count = min_t(size_t, count, MYDEVICE_BUF_SIZE - used);
	first = min_t(size_t, count, MYDEVICE_BUF_SIZE - head);
	second = count - first;
	
	ret = copy_from_user(mydevice_buf + head, buf, first);
	copied = first - ret;
	
	if(copied == 0) {
	mutex_unlock(&mydevice_mutex);
	return -EFAULT;
	}
	
	if(copied != first) goto out;
	
	if(second > 0) {
	  ret = copy_from_user(mydevice_buf, buf + first, second);
	  copied = copied + second - ret;
	  }
	  
out:
	head = (head + copied) % MYDEVICE_BUF_SIZE;
	used += copied;
	
mutex_unlock(&mydevice_mutex);

	return copied ? copied : -EFAULT;
}

static const struct file_operations mydevice_misc_fops = {
	.owner		= THIS_MODULE,
	.read           = mydevice_read,
	.write          = mydevice_write,

};


static const struct of_device_id mydevice_of_match[] = {
  { .compatible = "demo,mydevice" },
  { }
};
  
  
static int mydevice_probe(struct platform_device *pdev)
{
  struct mydevice_data *data;
  int ret;
  int value;
  
  
  data = kzalloc(sizeof(*data), GFP_KERNEL);
  if (!data) {
	  return -ENOMEM;
	}

  data->control_gpio = gpiod_get(&pdev->dev, "control", GPIOD_OUT_LOW);
  if (IS_ERR(data->control_gpio)) {
    int err = PTR_ERR(data->control_gpio);
    dev_err(&pdev->dev, "Failed to get control GPIO: %d\n", err);
    kfree(data);
    return err;
    }
    
  platform_set_drvdata(pdev, data);
  
  gpiod_set_value(data->control_gpio, 1);
  
  value = gpiod_get_value(data->control_gpio);
  if(value < 0)
      dev_err(&pdev->dev, "GPIO read failed: %d\n", value);
  else
      pr_info("GPIO 0 is [%d].\n", value);
  
  data->misdev.minor  = MISC_DYNAMIC_MINOR;
  data->misdev.name   = "mydevice";
  data->misdev.fops   = &mydevice_misc_fops;
  
  ret = misc_register(&data->misdev);
    	if (ret) {
		pr_err("mydevice: failed\n");
		gpiod_put(data->control_gpio);
		kfree(data);
		return ret;
	}
  pr_info("mydevice: successfully\n");
  return 0;
}

static void mydevice_remove(struct platform_device *pdev)
{
  struct mydevice_data *data = platform_get_drvdata(pdev);
  
  misc_deregister(&data->misdev);
  gpiod_put(data->control_gpio);
  kfree(data);
  
  pr_info("mydevice: unregistered successfully\n");  
}

static struct platform_driver mydevice_driver = {
    .probe = mydevice_probe,
    .remove = mydevice_remove,
    .driver = {
        .name = "mydevice",
        .of_match_table = mydevice_of_match,
    }
};


static int __init mydevice_module_init(void)
{
	int ret;

	ret = platform_driver_register(&mydevice_driver);
	if (ret) {
		pr_err("mydevice: platform_driver failed\n");
		return ret;
	}

	pr_info("mydevice: platform_driver successfully\n");
	return 0;
}

static void __exit mydevice_module_exit(void)
{
	platform_driver_unregister(&mydevice_driver);
	pr_info("mydevice: unregistered successfully\n");
}

module_init(mydevice_module_init);
module_exit(mydevice_module_exit);

MODULE_LICENSE("GPL");
