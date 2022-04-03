#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

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
    *(unsigned long *)(disk + MBR_DISK_SIGNATURE_OFFSET) = 0x36E5756D;
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
	spinlock_t lock;
	struct request_queue *queue;
	struct gendisk *gd;
} device;

static int bdev_open(struct block_device *bdev, fmode_t mode)
{
	printk(KERN_INFO "mydiskdrive : open \n");
	return 0;
}

static void bdev_release(struct gendisk *gd, fmode_t mode)
{
	printk(KERN_INFO "mydiskdrive : closed \n");
}

static struct block_device_operations fops =
	{
		.owner = THIS_MODULE,
		.open = bdev_open,
		.release = bdev_release,
};

static int rb_transfer(struct request *req)
{
	int dir = rq_data_dir(req);
	int ret = 0;
	/*starting sector
	 *where to do operation*/
	sector_t start_sector = blk_rq_pos(req);
	unsigned int sector_cnt = blk_rq_sectors(req); /* no of sector on which opn to be done*/
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
		printk(KERN_DEBUG "my disk: Start Sector: %llu, Sector Offset: %llu;\
		Buffer: %p; Length: %u sectors\n",
			   (unsigned long long)(start_sector), (unsigned long long)(sector_offset), buffer, sectors);

		if (dir == WRITE) /* Write to the device */
		{
			memcpy((device.data) + ((start_sector + sector_offset) * SECTOR_SIZE), buffer, sectors * SECTOR_SIZE);
		}
		else /* Read from the device */
		{
			memcpy(buffer, (device.data) + ((start_sector + sector_offset) * SECTOR_SIZE), sectors * SECTOR_SIZE);
		}
		sector_offset += sectors;
	}

	if (sector_offset != sector_cnt)
	{
		printk(KERN_ERR "mydisk: bio info doesn't match with the request info");
		ret = -EIO;
	}
	return ret;
}

/** request handling function**/
static void dev_request(struct request_queue *q)
{
	struct request *req;
	int error;
	while ((req = blk_fetch_request(q)) != NULL)
	{
		error = rb_transfer(req);
		__blk_end_request_all(req, error);
	}
}

//------------------------------------------------------------------------

static int ramdisk_init(void)
{
	(device.data) = vmalloc(MEMSIZE * SECTOR_SIZE);
	copy_mbr_n_br(device.data);
	return MEMSIZE;
}

static void ramdisk_cleanup(void)
{
	vfree(device.data);
}

static int __init lab2_init(void)
{
	device.size = ramdisk_init();
	printk(KERN_INFO "THIS IS DEVICE SIZE %d", device.size);
	
	if ((major = register_blkdev(0, DEV_NAME)) < 0)
		goto out_register;

	printk(KERN_INFO "Major Number is : %d", major);
	spin_lock_init(&device.lock);
	
	if (!(device.queue = blk_init_queue(dev_request, &device.lock)))
		goto out_init_queue;

	if (!(device.gd = alloc_disk(8)))
		goto out_alloc_gendisk;

	device.gd->major = major;
	device.gd->first_minor = 0;
	device.gd->fops = &fops;
	device.gd->private_data = &device;
	device.gd->queue = device.queue;
	
	sprintf(((device.gd)->disk_name), DEV_NAME);
	set_capacity(device.gd, device.size);
	add_disk(device.gd);
	return 0;

out_alloc_gendisk:
	blk_cleanup_queue(device.queue);
	printk(KERN_INFO "Failed alloc disk\n");
out_init_queue:
	unregister_blkdev(major, DEV_NAME);
	printk(KERN_INFO "Failed init queue\n");
out_register:
	ramdisk_cleanup();
	printk(KERN_INFO "Failed register\n");
	return -1;
}

static void __exit lab2_exit(void)
{
	del_gendisk(device.gd);
	put_disk(device.gd);
	blk_cleanup_queue(device.queue);
	unregister_blkdev(major, DEV_NAME);
	ramdisk_cleanup();
}

//------------------------------------------------------------------------

module_init(lab2_init);
module_exit(lab2_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("luribina");
MODULE_DESCRIPTION("io lab2");
MODULE_VERSION("1.0");
