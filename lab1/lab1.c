#include <linux/cdev.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/filter.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "parser.h"

/*
 * equation parser structs and functions
 */

static int parse_equation(const char *equation)
{
    int res;
    int postfix[PARSER_CAPACITY];
    int count = infix_to_postfix(equation, postfix);
    res = postfix_to_eval(postfix, count);
    return res;
}

/*
 * proc file structs and functions
 */

#define PROC_FILE_NAME "var2"
#define START_MESSAGE "Calculated results:\n"
#define MESSAGE_SIZE 1024
static char message[MESSAGE_SIZE] = START_MESSAGE;
static size_t message_len = sizeof(START_MESSAGE);

static struct proc_dir_entry *lab1_file;

static ssize_t lab1_read(struct file *file_ptr, char __user *ubuffer, size_t buf_length, loff_t *offset)
{
    size_t len = min(message_len - (size_t)*offset, buf_length);

    pr_info("Proc file read\n");

    if (len <= 0)
    {
        pr_info("All done\n");
        return 0;
    }

    if (copy_to_user(ubuffer, message + *offset, len))
    {
        pr_info("Didnt copy buffer message\n");
        return -EFAULT;
    }

    *offset += len;
    return len;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
#define HAVE_PROC_OPS
#endif

#ifdef HAVE_PROC_OPS
static const struct proc_ops proc_file_ops = {
    .proc_read = lab1_read};
#else
static const struct file_operations proc_file_ops = {
    .owner = THIS_MODULE,
    .read = lab1_read};
#endif

/*
 * char dev structs and functions
 */

#define DEVICE_NAME "lab1_chrdev"
#define CLASS_NAME "lab1_class"
#define DEV_NAME "lab1_dev%d"
#define DEV_COUNT 4

#define BUF_SIZE 128
static char number_message[BUF_SIZE];

static dev_t maj_min;
static struct class *cls;

enum
{
    CDEV_NOT_USED = 0,
    CDEV_EXCLUSIVE_OPEN = 1,
};

static atomic_t already_open = ATOMIC_INIT(CDEV_NOT_USED);

static int lab1_dev_open(struct inode *inode, struct file *f)
{
    if (atomic_cmpxchg(&already_open, CDEV_NOT_USED, CDEV_EXCLUSIVE_OPEN))
    {
        return -EBUSY;
    }
    try_module_get(THIS_MODULE);
    return 0;
}

static int lab1_dev_release(struct inode *inode, struct file *f)
{
    atomic_set(&already_open, CDEV_NOT_USED);
    module_put(THIS_MODULE);
    return 0;
}

static ssize_t lab1_dev_read(struct file *file_ptr, char __user *ubuffer, size_t buf_length, loff_t *offset)
{
    pr_info("%s\n", message);
    return 0;
}

static ssize_t lab1_dev_write(struct file *file_ptr, const char __user *ubuffer, size_t buf_length, loff_t *offset)
{
    char number[BUF_SIZE];
    size_t len = buf_length;
    int res;

    pr_info("Write to dev\n");

    if (buf_length > BUF_SIZE)
    {
        len = BUF_SIZE;
    }

    if (copy_from_user(number_message, ubuffer, len))
    {
        return -EFAULT;
    }

    res = parse_equation(number_message);
    sprintf(number, "%d\n", res);
    message_len += strlen(number);
    if (message_len < MESSAGE_SIZE)
    {
        strcat(message, number);
    }
    else
    {
        pr_info("Not enough space to write new information\n");
    }
    return len;
}

static int cls_uevent(struct device *dev, struct kobj_uevent_env *env)
{
    add_uevent_var(env, "DEVMODE=%#o", 0666);
    return 0;
}

static struct file_operations lab1_dev_fops =
    {
        .owner = THIS_MODULE,
        .open = lab1_dev_open,
        .release = lab1_dev_release,
        .read = lab1_dev_read,
        .write = lab1_dev_write};

/*
 * module init and exit
 */

static int full = 0;

static dev_t maj_mins[DEV_COUNT];
static struct cdev cdevs[DEV_COUNT];

static void clear_all_full(void)
{
    int i = 0;
	for (i = 0; i < full; i++) {
		device_destroy(cls, maj_mins[i]);
		cdev_del(&cdevs[i]);
	}
	class_destroy(cls);
	unregister_chrdev_region(maj_min, DEV_COUNT);
}

static int __init lab1_init(void)
{
    int i = 0;
    int major;

    pr_info("Loaded lab1 module\n");

    if (alloc_chrdev_region(&maj_min, 0, DEV_COUNT, DEVICE_NAME) < 0)
    {
        pr_alert("Can not alloc chrdev region\n");
        return -1;
    }

    major = MAJOR(maj_min);
    cls = class_create(THIS_MODULE, CLASS_NAME);
    if (cls == NULL)
    {
        pr_alert("Can not create class\n");
        unregister_chrdev_region(maj_min, 1);
        return -1;
    }
    cls->dev_uevent = cls_uevent;

    for (i = 0; i < DEV_COUNT; i++)
    {
        maj_mins[i] = MKDEV(major, i);
        cdev_init(&cdevs[i], &lab1_dev_fops);

        if (cdev_add(&cdevs[i], maj_mins[i], 1) < 0)
        {
            pr_alert("Can not add char device\n");
            clear_all_full();
        }

        if (device_create(cls, NULL, maj_mins[i], NULL, DEV_NAME, i) == NULL)
        {
            pr_alert("Can not create device\n");
            cdev_del(&cdevs[i]);
            clear_all_full();
            return -1;
        }

        full++;
    }

    lab1_file = proc_create(PROC_FILE_NAME, 0444, NULL, &proc_file_ops);

    if (lab1_file == NULL)
    {
        pr_alert("Can not create file for some reason\n");
        clear_all_full();
        return -1;
    }

    pr_info("Success!\n");
    return 0;
}

static void __exit lab1_exit(void)
{
    proc_remove(lab1_file);
    clear_all_full();
    pr_info("Unloaded lab1 module\n");
}

module_init(lab1_init);
module_exit(lab1_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("luribina");
MODULE_DESCRIPTION("io lab1");
MODULE_VERSION("1.0");
