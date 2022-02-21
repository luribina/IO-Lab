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

/////////////////////////////////////////////////
// equation parser structs and functions
/////////////////////////////////////////////////

static int size = 0;
static int results[PARSER_CAPACITY];
static int postfix[PARSER_CAPACITY];

static void parse_equation(const char *equation)
{
    int res = 0;
    infix_to_postfix(equation, postfix);
    res = postfix_to_eval(postfix);
    results[size++] = res;
    return;
}

/////////////////////////////////////////////////
// proc file structs and functions
/////////////////////////////////////////////////

#define PROC_FILE_NAME "var2"
#define START_MESSAGE "Calculated results:\n"
#define MESSAGE_SIZE 100

static struct proc_dir_entry *lab1_file;

static ssize_t lab1_read(struct file *file_ptr, char __user *ubuffer, size_t buf_length, loff_t *offset)
{
    int len, i;
    char message[MESSAGE_SIZE] = START_MESSAGE;
    char number[MESSAGE_SIZE];
    pr_info("Proc file read\n");

    if (*offset > 0)
    {
        pr_info("All done\n");
        return 0;
    }

    if (buf_length < MESSAGE_SIZE)
    {
        pr_info("Buffer not enough size\n");
        return 0;
    }

    for (i = 0; i < size; i++)
    {
        sprintf(number, "%d\n", results[i]);
        strcat(message, number);
    }

    len = strlen(message) + 1;
    *offset += len;
    if (copy_to_user(ubuffer, message, len))
    {
        pr_info("Didnt copy buffer message\n");
        return -EFAULT;
    }
    return *offset;
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

/////////////////////////////////////////////////
// char dev structs and functions
/////////////////////////////////////////////////

#define DEVICE_NAME "lab1_chrdev"
#define CLASS_NAME "lab1_class"
#define DEV_NAME "lab1_dev"

#define BUF_SIZE 128
static char message[BUF_SIZE];

static dev_t maj_min;
static struct cdev cdev;
static struct class *cls;

static ssize_t lab1_dev_read(struct file *file_ptr, char __user *ubuffer, size_t buf_length, loff_t *offset)
{
    size_t i;
    pr_info("Calculated results:\n");

    for (i = 0; i < size; i++)
    {
        pr_info("%d\n", results[i]);
    }
    return 0;
}

static ssize_t lab1_dev_write(struct file *file_ptr, const char __user *ubuffer, size_t buf_length, loff_t *offset)
{
    size_t len = buf_length;

    pr_info("Write to dev\n");

    if (buf_length > BUF_SIZE)
    {
        len = BUF_SIZE;
    }

    if (copy_from_user(message, ubuffer, len))
    {
        return -EFAULT;
    }

    parse_equation(message);

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
        .read = lab1_dev_read,
        .write = lab1_dev_write};

/////////////////////////////////////////////////
// module init and exit
/////////////////////////////////////////////////

static int __init lab1_init(void)
{
    pr_info("Loaded lab1 module\n");

    if (alloc_chrdev_region(&maj_min, 0, 1, DEVICE_NAME) < 0)
    {
        pr_alert("Can not alloc chrdev region\n");
        return -1;
    }

    cls = class_create(THIS_MODULE, CLASS_NAME);
    if (cls == NULL)
    {
        pr_alert("Can not create class\n");
        unregister_chrdev_region(maj_min, 1);
        return -1;
    }
    cls->dev_uevent = cls_uevent;

    if (device_create(cls, NULL, maj_min, NULL, DEV_NAME) == NULL)
    {
        pr_alert("Can not create device\n");
        class_destroy(cls);
        unregister_chrdev_region(maj_min, 1);
        return -1;
    }

    cdev_init(&cdev, &lab1_dev_fops);
    if (cdev_add(&cdev, maj_min, 1) < 0)
    {
        pr_alert("Can not add char device\n");
        device_destroy(cls, maj_min);
        class_destroy(cls);
        unregister_chrdev_region(maj_min, 1);
        return -1;
    }

    lab1_file = proc_create(PROC_FILE_NAME, 0444, NULL, &proc_file_ops);

    if (lab1_file == NULL)
    {
        pr_alert("Can not create file for some reason\n");
        cdev_del(&cdev);
        device_destroy(cls, maj_min);
        class_destroy(cls);
        unregister_chrdev_region(maj_min, 1);
        return -1;
    }

    pr_info("Success!\n");
    return 0;
}

static void __exit lab1_exit(void)
{
    proc_remove(lab1_file);
    cdev_del(&cdev);
    device_destroy(cls, maj_min);
    class_destroy(cls);
    unregister_chrdev_region(maj_min, 1);
    pr_info("Unloaded lab1 module\n");
}

module_init(lab1_init);
module_exit(lab1_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("luribina");
MODULE_DESCRIPTION("io lab1");
MODULE_VERSION("1.0");
