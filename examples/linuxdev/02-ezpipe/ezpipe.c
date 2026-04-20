#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#define DEV_NAME "ezpipe"
#define EZPIPE_BUF_SIZE 256

static dev_t ez_devno;
static struct cdev ez_cdev;
static struct class *ez_class;

static char ez_buf[EZPIPE_BUF_SIZE];
static size_t ez_len;
static DEFINE_MUTEX(ez_lock);

static int ez_open(struct inode *inode, struct file *filp)
{
	pr_info("ezpipe: open\n");
	return 0;
}

static int ez_release(struct inode *inode, struct file *filp)
{
	pr_info("ezpipe: release\n");
	return 0;
}

static ssize_t ez_read(struct file *filp, char __user *buf,
		       size_t count, loff_t *ppos)
{
	size_t available;

	mutex_lock(&ez_lock);

	if (*ppos >= ez_len) {
		mutex_unlock(&ez_lock);
		return 0;
	}

	available = ez_len - *ppos;
	if (count > available)
		count = available;

	if (copy_to_user(buf, ez_buf + *ppos, count)) {
		mutex_unlock(&ez_lock);
		return -EFAULT;
	}

	*ppos += count;
	mutex_unlock(&ez_lock);
	return count;
}

static ssize_t ez_write(struct file *filp, const char __user *buf,
			size_t count, loff_t *ppos)
{
	size_t to_copy = count;

	if (to_copy > EZPIPE_BUF_SIZE - 1)
		to_copy = EZPIPE_BUF_SIZE - 1;

	mutex_lock(&ez_lock);

	if (copy_from_user(ez_buf, buf, to_copy)) {
		mutex_unlock(&ez_lock);
		return -EFAULT;
	}

	ez_buf[to_copy] = '\0';
	ez_len = to_copy;
	*ppos = 0;

	mutex_unlock(&ez_lock);
	return to_copy;
}

static const struct file_operations ez_fops = {
	.owner = THIS_MODULE,
	.open = ez_open,
	.release = ez_release,
	.read = ez_read,
	.write = ez_write,
};

static int __init ez_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&ez_devno, 0, 1, DEV_NAME);
	if (ret)
		return ret;

	cdev_init(&ez_cdev, &ez_fops);
	ez_cdev.owner = THIS_MODULE;

	ret = cdev_add(&ez_cdev, ez_devno, 1);
	if (ret)
		goto err_unregister;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	ez_class = class_create(DEV_NAME);
#else
	ez_class = class_create(THIS_MODULE, DEV_NAME);
#endif
	if (IS_ERR(ez_class)) {
		ret = PTR_ERR(ez_class);
		goto err_cdev_del;
	}

	if (IS_ERR(device_create(ez_class, NULL, ez_devno, NULL, DEV_NAME))) {
		ret = -EINVAL;
		goto err_class_destroy;
	}

	pr_info("ezpipe: registered major=%d minor=%d\n",
		MAJOR(ez_devno), MINOR(ez_devno));
	return 0;

err_class_destroy:
	class_destroy(ez_class);
err_cdev_del:
	cdev_del(&ez_cdev);
err_unregister:
	unregister_chrdev_region(ez_devno, 1);
	return ret;
}

static void __exit ez_exit(void)
{
	device_destroy(ez_class, ez_devno);
	class_destroy(ez_class);
	cdev_del(&ez_cdev);
	unregister_chrdev_region(ez_devno, 1);
	pr_info("ezpipe: unloaded\n");
}

module_init(ez_init);
module_exit(ez_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EASYZOOM");
MODULE_DESCRIPTION("Minimal char device example with read/write");
