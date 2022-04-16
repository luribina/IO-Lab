#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/version.h>


//------------------------------------------------------------------------

/*
    MBR description
*/

#define SECTOR_SIZE 512
#define MBR_SIZE SECTOR_SIZE
#define MBR_DISK_SIGNATURE_OFFSET 440
#define MBR_DISK_SIGNATURE_SIZE 4
#define PARTITION_TABLE_OFFSET 446
#define PARTITION_ENTRY_SIZE 16
#define PARTITION_TABLE_SIZE 64
#define MBR_SIGNATURE_OFFSET 510
#define MBR_SIGNATURE_SIZE 2
#define MBR_SIGNATURE 0xAA55
#define BR_SIZE SECTOR_SIZE
#define BR_SIGNATURE_OFFSET 510
#define BR_SIGNATURE_SIZE 2
#define BR_SIGNATURE 0xAA55

#define SEC_PER_HEAD 63
#define HEAD_PER_CYL 255
#define HEAD_SIZE (SEC_PER_HEAD * SECTOR_SIZE)
#define CYL_SIZE (SEC_PER_HEAD * HEAD_PER_CYL * SECTOR_SIZE)

#define sec4size(s) ((((s) % CYL_SIZE) % HEAD_SIZE) / SECTOR_SIZE)
#define head4size(s) (((s) % CYL_SIZE) / HEAD_SIZE)
#define cyl4size(s) ((s) / CYL_SIZE)

#define PART1_SIZE (10 * 2048) // part1 size in sectors
#define PART2_SIZE (20 * 2048) // part2 size in sectors
#define PART3_SIZE (20 * 2048) // part3 size in sectors (extended)
#define PART31_SIZE (10 * 2048) // part1 size in sectors (belongs to part3)
#define PART32_SIZE (10 * 2048) // part2 size in sectors (belongs to part3)

#define MEMSIZE (PART1_SIZE + PART2_SIZE + PART3_SIZE + 3) // Size of Ram disk in sectors

//------------------------------------------------------------------------

/*
    Partition table description
*/

typedef struct
{
    u8 boot_type; // 0x00 - Inactive; 0x80 - Active (Bootable)
    u8 start_head;
    u8 start_sec : 6;
    u8 start_cyl_hi : 2;
    u8 start_cyl;
    u8 part_type;
    u8 end_head;
    u8 end_sec : 6;
    u8 end_cyl_hi : 2;
    u8 end_cyl;
    u32 abs_start_sec;
    u32 sec_in_part;
} PartEntry;

typedef PartEntry PartTable[4];


static PartTable def_part_table = {
	{
		boot_type : 0x00,
		start_sec : 0x1,
		start_head : 0x0,
		start_cyl_hi : 0x0,
		start_cyl : 0x0,
		part_type : 0x83,
		end_sec : sec4size(PART1_SIZE - 1) + 1,
		end_head : head4size(PART1_SIZE - 1),
		end_cyl_hi : (cyl4size(PART1_SIZE - 1) >> 8) & 0x3,
		end_cyl : cyl4size(PART1_SIZE - 1) & 0xFF,
		abs_start_sec : 0x1,
		sec_in_part : PART1_SIZE
	},
	{
		boot_type : 0x00,
		start_sec : sec4size(PART1_SIZE) + 1,
		start_head : head4size(PART1_SIZE),
		start_cyl_hi : (cyl4size(PART1_SIZE) >> 8) & 0x3,
		start_cyl : cyl4size(PART1_SIZE) & 0xFF,
		part_type : 0x83,
		end_sec : sec4size(PART1_SIZE + PART2_SIZE - 1) + 1,
		end_head : head4size(PART1_SIZE + PART2_SIZE - 1),
		end_cyl_hi : (cyl4size(PART1_SIZE + PART2_SIZE - 1) >> 8) & 0x3,
		end_cyl : cyl4size(PART1_SIZE + PART2_SIZE - 1) & 0xFF,
		abs_start_sec : PART1_SIZE + 1,
		sec_in_part : PART2_SIZE
	},
	// extended
	{
		boot_type : 0x00,
		start_sec : sec4size(PART1_SIZE + PART2_SIZE) + 1,
		start_head : head4size(PART1_SIZE + PART2_SIZE),
		start_cyl_hi : (cyl4size(PART1_SIZE + PART2_SIZE) >> 8) & 0x3,
		start_cyl : cyl4size(PART1_SIZE + PART2_SIZE) & 0xFF,
		part_type : 0x05,
		end_sec : sec4size(PART1_SIZE + PART2_SIZE + PART3_SIZE - 1) + 1,
		end_head : head4size(PART1_SIZE + PART2_SIZE + PART3_SIZE - 1),
		end_cyl_hi : (cyl4size(PART1_SIZE + PART2_SIZE + PART3_SIZE - 1) >> 8) & 0x3,
		end_cyl : cyl4size(PART1_SIZE + PART2_SIZE + PART3_SIZE - 1) & 0xFF,
		abs_start_sec : PART1_SIZE + PART2_SIZE + 1,
		sec_in_part : PART3_SIZE + 2
	}};

static unsigned int def_log_part_br_abs_start_sector[] = {
	PART1_SIZE + PART2_SIZE + 1,
	PART1_SIZE + PART2_SIZE + PART31_SIZE + 2,
};

static const PartTable def_log_part_table[] = {
	{{
		 boot_type : 0x00,
		 start_head : 0,
		 start_sec : 1,
		 start_cyl_hi : 0,
		 start_cyl : 0,
		 part_type : 0x83,
		 end_sec : sec4size(PART31_SIZE - 1) + 1,
		 end_head : head4size(PART31_SIZE - 1),
		 end_cyl_hi : (cyl4size(PART31_SIZE - 1) >> 8) & 0x3,
		 end_cyl : cyl4size(PART31_SIZE - 1) & 0xFF,
		 abs_start_sec : 0x1,
		 sec_in_part : PART31_SIZE
	 },
	 {
		 boot_type : 0x00,
		 start_head : sec4size(PART31_SIZE) + 1,
		 start_sec : head4size(PART31_SIZE),
		 start_cyl_hi : (cyl4size(PART31_SIZE) >> 8) & 0x3,
		 start_cyl : cyl4size(PART31_SIZE) & 0xFF,
		 part_type : 0x05,
		 end_sec : sec4size(PART31_SIZE + PART32_SIZE - 1) + 1,
		 end_head : head4size(PART31_SIZE + PART32_SIZE - 1),
		 end_cyl_hi : (cyl4size(PART31_SIZE + PART32_SIZE - 1) >> 8) & 0x3,
		 end_cyl : cyl4size(PART31_SIZE + PART32_SIZE - 1) & 0xFF,
		 abs_start_sec : PART31_SIZE + 1,
		 sec_in_part : PART32_SIZE
	 }},
	{{
		boot_type : 0x00,
		start_head : 0,
		start_sec : 1,
		start_cyl_hi : 0,
		start_cyl : 0,
		part_type : 0x83,
		end_sec : sec4size(PART32_SIZE - 1) + 1,
		end_head : head4size(PART32_SIZE - 1),
		end_cyl_hi : (cyl4size(PART32_SIZE - 1) >> 8) & 0x3,
		end_cyl : cyl4size(PART32_SIZE - 1) & 0xFF,
		abs_start_sec : 0x1,
		sec_in_part : PART32_SIZE
	}}};

//------------------------------------------------------------------------

/*
    Init RAM disk functions
*/

static void copy_mbr(u8 *disk)
{
    memset(disk, 0x0, MBR_SIZE);
    memcpy(disk + PARTITION_TABLE_OFFSET, &def_part_table, PARTITION_TABLE_SIZE);
    *(unsigned short *)(disk + MBR_SIGNATURE_OFFSET) = MBR_SIGNATURE;
}

static void copy_br(u8 *disk, int abs_start_sector, const PartTable *part_table)
{
    disk += (abs_start_sector * SECTOR_SIZE);
    memset(disk, 0x0, BR_SIZE);
    memcpy(disk + PARTITION_TABLE_OFFSET, part_table,
           PARTITION_TABLE_SIZE);
    *(unsigned short *)(disk + BR_SIGNATURE_OFFSET) = BR_SIGNATURE;
}

static void copy_mbr_n_br(u8 *disk)
{
    int i;
    copy_mbr(disk);
    for (i = 0; i < ARRAY_SIZE(def_log_part_table); i++)
    {
        copy_br(disk, def_log_part_br_abs_start_sector[i], &def_log_part_table[i]);
    }
}

//------------------------------------------------------------------------

/*
	Block device functions and structures
*/

#define DEV_NAME "lab2"

static int major = 0;

static struct ram_device
{
	unsigned int size;
	u8 * data;
	struct blk_mq_tag_set tag_set;
	struct request_queue *queue;
	struct gendisk *gd;
} device;

static int bdev_open(struct block_device *bdev, fmode_t mode)
{
	printk(DEV_NAME " : open \n");
	return 0;
}

static void bdev_release(struct gendisk *gd, fmode_t mode)
{
	printk(DEV_NAME " : closed \n");
}

static struct block_device_operations fops =
	{
		.owner = THIS_MODULE,
		.open = bdev_open,
		.release = bdev_release,
};

static int rb_transfer(struct request *req, unsigned int *nr_bytes)
{
	int dir = rq_data_dir(req);
	int ret = 0;
	sector_t start_sector = blk_rq_pos(req);
	unsigned int sector_cnt = blk_rq_sectors(req);
	struct bio_vec bv;
#define BV_PAGE(bv) ((bv).bv_page)
#define BV_OFFSET(bv) ((bv).bv_offset)
#define BV_LEN(bv) ((bv).bv_len)
	struct req_iterator iter;
	sector_t sector_offset;
	unsigned int sectors;
	u8 *buffer;
	sector_offset = 0;
	rq_for_each_segment(bv, req, iter)
	{
		buffer = page_address(BV_PAGE(bv)) + BV_OFFSET(bv);
		if (BV_LEN(bv) % (SECTOR_SIZE) != 0)
		{
			printk(KERN_ERR "bio size is not a multiple ofsector size\n");
			ret = -EIO;
		}
		sectors = BV_LEN(bv) / SECTOR_SIZE;

		if (dir == WRITE)
		{
			memcpy((device.data) + ((start_sector + sector_offset) * SECTOR_SIZE), buffer, sectors * SECTOR_SIZE);
		}
		else
		{
			memcpy(buffer, (device.data) + ((start_sector + sector_offset) * SECTOR_SIZE), sectors * SECTOR_SIZE);
		}
		sector_offset += sectors;
		*nr_bytes += BV_LEN(bv);
	}

	if (sector_offset != sector_cnt)
	{
		printk("mydisk: bio info doesn't match with the request info");
		ret = -EIO;
	}
	return ret;
}

static blk_status_t queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data* bd)
{
    unsigned int nr_bytes = 0;
    blk_status_t status = BLK_STS_OK;
    struct request *rq = bd->rq;

    /* Start request serving procedure */
    blk_mq_start_request(rq);

    if (rb_transfer(rq, &nr_bytes) != 0) {
        status = BLK_STS_IOERR;
    }

    /* Notify kernel about processed nr_bytes */
    if (blk_update_request(rq, status, nr_bytes)) {
        /* Shouldn't fail */
        BUG();
    }

    /* Stop request serving procedure */
    __blk_mq_end_request(rq, status);

    return status;
}

static struct blk_mq_ops mq_ops = {
    .queue_rq = queue_rq,
};

//------------------------------------------------------------------------

/*
	Proc file structs and functions
*/

#define BUF_MIN_SIZE 128
#define PROC_FILE_NAME "lab2out"

static struct proc_dir_entry *out_file;
static char lab2_write_buffer[BUF_MIN_SIZE];

static ssize_t proc_write(struct file *file_ptr, const char __user *ubuffer, size_t buf_length, loff_t *offset);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
#define HAVE_PROC_OPS
#endif

#ifdef HAVE_PROC_OPS
static const struct proc_ops proc_file_ops = {
    .proc_write = proc_write
};
#else
static const struct file_operations proc_file_ops = {
    .owner = THIS_MODULE,
    .write = proc_write
};
#endif

//------------------------------------------------------------------------

static int ramdisk_init(int block_count)
{
	(device.data) = vmalloc(block_count * SECTOR_SIZE);
	// copy_mbr_n_br(device.data);
	return block_count;
}

static void ramdisk_cleanup(void)
{
	vfree(device.data);
}

static int device_init(int block_count)
{
	device.size = ramdisk_init(block_count);
	printk(KERN_INFO "THIS IS DEVICE SIZE %d", device.size);

	if ((major = register_blkdev(0, DEV_NAME)) < 0)
	{
		printk("Failed to register block_dev\n");
		ramdisk_cleanup();
		return -ENOMEM;
	}

	printk("Major Number is : %d", major);
	if (!(device.queue = blk_mq_init_sq_queue(&device.tag_set, &mq_ops, 128, BLK_MQ_F_SHOULD_MERGE)))
	{
		printk("Failed init queue\n");
		unregister_blkdev(major, DEV_NAME);
		ramdisk_cleanup();
		return -ENOMEM;
	}

    device.queue->queuedata = &device;

    if (!(device.gd = alloc_disk(1))) {
		printk(KERN_INFO "Failed alloc disk\n");
		blk_cleanup_queue(device.queue);
		unregister_blkdev(major, DEV_NAME);
		ramdisk_cleanup();
		return -ENOMEM;
	}

	device.gd->flags = GENHD_FL_NO_PART_SCAN;
    device.gd->major = major;
    device.gd->first_minor = 0;
	device.gd->fops = &fops;
	device.gd->queue = device.queue;
	device.gd->private_data = &device;

    sprintf(((device.gd)->disk_name), DEV_NAME);
	set_capacity(device.gd, device.size);
	add_disk(device.gd);

	return 0;
}

static void device_clean(void)
{
	del_gendisk(device.gd);
	put_disk(device.gd);
	blk_cleanup_queue(device.queue);
	unregister_blkdev(major, DEV_NAME);
	ramdisk_cleanup();
}

static int __init lab2_init(void)
{
	out_file = proc_create(PROC_FILE_NAME, 0666, NULL, &proc_file_ops);

    if (out_file == NULL) {
        pr_alert("Could not create file for some reason\n");
        return -EIO;
    }

	return device_init(MEMSIZE);
}

static void __exit lab2_exit(void)
{
	device_clean();
	proc_remove(out_file);
}

//------------------------------------------------------------------------

static ssize_t proc_write(struct file *file_ptr, const char __user *ubuffer, size_t buf_length, loff_t *offset)
{
	int r, disk_size;
	size_t len = buf_length;

	pr_info("Proc file %s is write start\n", PROC_FILE_NAME);

	if (buf_length > BUF_MIN_SIZE - 1)
	{
		len = BUF_MIN_SIZE - 1;
	}

	if (copy_from_user(lab2_write_buffer, ubuffer, len))
	{
		return -EFAULT;
	}

	r = sscanf(lab2_write_buffer, "%d", &disk_size);
	if (r != 1)
	{
		pr_info("Got non matching input\n");
		return buf_length;
	}

	if (disk_size <= 0)
	{
		pr_info("Size can not be below or equal zero, disk didn't change\n");
		return buf_length;
	}

	pr_info("New disk size is %d MB\n", disk_size);

	device_clean();
	device_init(disk_size * 2048);

	pr_info("Proc file %s is write end\n", PROC_FILE_NAME);

	return buf_length;
}

//------------------------------------------------------------------------

module_init(lab2_init);
module_exit(lab2_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("luribina");
MODULE_DESCRIPTION("io lab2");
MODULE_VERSION("1.0");


//      |\      _,,,---,,_
//      /,`.-'`'    -.  ;-;;,_
//     |,4-  ) )-,_. ,\ (  `'-'
//    '---''(_/--'  `-'\_) 
