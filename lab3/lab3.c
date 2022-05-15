#include <linux/etherdevice.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/udp.h>
#include <linux/version.h>
#include <net/arp.h>

//------------------------------------------------------------------------

/*
    Virtual network interface structs and functions
*/

static const char * devices[] = {"lo", "enp0s3", "enp0s8"};
#define devices_size (sizeof(devices) / sizeof(char *))

static char *link = "lo";
module_param(link, charp, 0);

static char *dest = "127.0.0.11";
module_param(dest, charp, 0);

static int dest1 = 127, dest2 = 0, dest3 = 0, dest4 = 11;

static char *ifname = "vni%d";

static struct net_device_stats stats;

static struct net_device *child = NULL;
struct priv
{
    struct net_device *interfaces[devices_size];
    struct net_device *parent;
};

#define MESSAGE_SIZE 4096
static char message[MESSAGE_SIZE];
static size_t message_len;

static char check_frame(struct sk_buff *skb, unsigned char data_shift)
{
    const struct iphdr *ip = (struct iphdr *)skb_network_header(skb);

    const u32 saddr = ntohl(ip->saddr);
    const u32 daddr = ntohl(ip->daddr);

    const int d1 = daddr >> 24,
              d2 = (daddr >> 16) & 0x00FF,
              d3 = (daddr >> 8) & 0x0000FF,
              d4 = (daddr)&0x000000FF;

    int len = 0;

    if (ip->version == 4 && d1==dest1 && d2==dest2 && d3==dest3 && d4==dest4) {
        char saddr_str[128];
        char daddr_str[128];

        len = sprintf(saddr_str, "saddr: %d.%d.%d.%d\n",
                saddr >> 24,
                (saddr >> 16) & 0x00FF,
                (saddr >> 8) & 0x0000FF,
                (saddr) & 0x000000FF);

        len += sprintf(daddr_str, "daddr: %d.%d.%d.%d\n\n", d1, d2, d3, d4);

        pr_info("%s", saddr_str);
        pr_info("%s", daddr_str);

        if (message_len + len >= MESSAGE_SIZE) {
            pr_info("Message buffer is full\n");
            strcpy(message, saddr_str);
            message_len = 0;
        } else {
            strcat(message, saddr_str);
        }
        strcat(message, daddr_str);

        message_len += len;

        return 1;
    }

    return 0;
}

static rx_handler_result_t handle_frame(struct sk_buff **pskb)
{
    struct sk_buff * skb = * pskb;
    if (check_frame(skb, 0))
    {
        stats.rx_packets++;
        stats.rx_bytes += skb->len;
    }
    return RX_HANDLER_PASS;
}

static int open(struct net_device *dev)
{
    netif_start_queue(dev);
    pr_info("%s: device opened", dev->name);
    return 0;
}

static int stop(struct net_device *dev)
{
    netif_stop_queue(dev);
    pr_info("%s: device closed", dev->name);
    return 0;
}

static netdev_tx_t start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct priv *priv = netdev_priv(dev);

    stats.tx_packets++;
    stats.tx_bytes += skb->len;

    if (priv->parent)
    {
        skb->dev = priv->parent;
        skb->priority = 1;
        dev_queue_xmit(skb);
    }
    return NETDEV_TX_OK;
}

static struct net_device_stats *get_stats(struct net_device *dev)
{
    return &stats;
}

static struct net_device_ops net_device_ops = {
    .ndo_open = open,
    .ndo_stop = stop,
    .ndo_get_stats = get_stats,
    .ndo_start_xmit = start_xmit};

static void setup(struct net_device *dev)
{
    ether_setup(dev);
    memset(netdev_priv(dev), 0, sizeof(struct priv));
    dev->netdev_ops = &net_device_ops;
}

static int fill_priv(struct priv * priv)
{
    int i = 0;
    for (i = 0; i < devices_size; ++i)
    {
        priv->interfaces[i] = __dev_get_by_name(&init_net, devices[i]);
        if (!priv->interfaces[i])
        {
            pr_err("%s: no such net: %s", THIS_MODULE->name, link);
            return -ENODEV;
        }

        if (priv->interfaces[i]->type != ARPHRD_ETHER && priv->interfaces[i]->type != ARPHRD_LOOPBACK)
        {
            pr_err("%s: illegal net type", THIS_MODULE->name);
            return -EINVAL;
        }
    }

    priv->parent = __dev_get_by_name(&init_net, link);
    if (!priv->parent)
    {
        pr_err("%s: no such net: %s", THIS_MODULE->name, link);
        return -ENODEV;
    }

    if (priv->parent->type != ARPHRD_ETHER && priv->parent->type != ARPHRD_LOOPBACK)
    {
        pr_err("%s: illegal net type", THIS_MODULE->name);
        return -EINVAL;
    }
    return 0;
}

int register_handlers(struct priv * priv)
{
    int i = 0;
    int err = 0;
    for (i = 0; i < devices_size; ++i)
    {
        rtnl_lock();
        err = netdev_rx_handler_register(priv->interfaces[i], handle_frame, NULL);
        rtnl_unlock();
        if (err != 0) {
            pr_info("error on registering handler for device %s\n", devices[i]);
            return err;
        }
    }
    return 0;
}

void unregister_handlers(struct priv * priv)
{
    int i = 0;
    for (i = 0; i < devices_size; ++i)
    {
        rtnl_lock();
        netdev_rx_handler_unregister(priv->interfaces[i]);
        rtnl_unlock();
    }
}

//------------------------------------------------------------------------

/*
    Proc device structs and functions
*/

#define PROC_FILE_NAME "var2"

static struct proc_dir_entry *lab3_file;

static ssize_t lab3_read(struct file *file_ptr, char __user *ubuffer, size_t buf_length, loff_t *offset)
{
    int len = min(message_len - (size_t)*offset, buf_length);

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
    .proc_read = lab3_read};
#else
static const struct file_operations proc_file_ops = {
    .owner = THIS_MODULE,
    .read = lab3_read};
#endif

//------------------------------------------------------------------------

/*
    Init and exit functions
*/

int __init lab3_init(void)
{
    int err = 0;
    struct priv *priv;
    u32 dest_addr = ntohl(in_aton(dest));

    child = alloc_netdev(sizeof(struct priv), ifname, NET_NAME_UNKNOWN, setup);
    
    if (child == NULL)
    {
        pr_err("%s: allocate error", THIS_MODULE->name);
        return -ENOMEM;
    }
    
    priv = netdev_priv(child);
    err = fill_priv(priv);
    if (err !=0) {
        free_netdev(child);
        return err;
    }

    // copy IP, MAC and other information
    memcpy(child->dev_addr, priv->parent->dev_addr, ETH_ALEN);
    memcpy(child->broadcast, priv->parent->broadcast, ETH_ALEN);
    
    if ((err = dev_alloc_name(child, child->name)))
    {
        pr_err("%s: allocate name, error %i", THIS_MODULE->name, err);
        free_netdev(child);
        return -EIO;
    }

    err = register_handlers(priv);
    if (err !=0) {
        free_netdev(child);
        return err;
    }

    register_netdev(child);

    lab3_file = proc_create(PROC_FILE_NAME, 0444, NULL, &proc_file_ops);

    if (lab3_file == NULL)
    {
        pr_alert("Can not create file for some reason\n");
        unregister_handlers(priv);
        unregister_netdev(child);
        free_netdev(child);
        return -1;
    }

    dest1 = dest_addr >> 24;
    dest2 = (dest_addr >> 16) & 0x00FF;
    dest3 = (dest_addr >> 8) & 0x0000FF;
    dest4 = (dest_addr) & 0x000000FF;
    pr_info("Init addr: %d.%d.%d.%d\n", dest1, dest2, dest3, dest4);

    pr_info("Module %s loaded", THIS_MODULE->name);
    pr_info("%s: create link %s", THIS_MODULE->name, child->name);
    pr_info("%s: registered rx handler for %s", THIS_MODULE->name, priv->parent->name);
    return 0;
}

void __exit lab3_exit(void)
{
    struct priv *priv = netdev_priv(child);
    proc_remove(lab3_file);
    unregister_handlers(priv);

    unregister_netdev(child);
    free_netdev(child);
    pr_info("Module %s unloaded", THIS_MODULE->name);
}

//------------------------------------------------------------------------

module_init(lab3_init);
module_exit(lab3_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("luribina");
MODULE_DESCRIPTION("io lab3");
MODULE_VERSION("1.0");


//      |\      _,,,---,,_
//      /,`.-'`'    -.  ;-;;,_
//     |,4-  ) )-,_. ,\ (  `'-'
//    '---''(_/--'  `-'\_) 
