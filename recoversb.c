/*
 * clear_sb_ro.c - Example kernel module to clear SB_RDONLY on a mounted ext4 FS
 *
 * Build:
 *   make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
 *
 * Install and load:
 *   sudo insmod clear_sb_ro.ko
 *   echo "/dev/sda1" > /sys/module/clear_sb_ro/parameters/clear_device
 *   # Then try: mount -o remount,rw /mountpoint
 *
 * Remove:
 *   sudo rmmod clear_sb_ro
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>         // struct super_block, iterate_supers()
#include <linux/mount.h>      // FS_REQUIRES_DEV, etc.
#include <linux/string.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/namei.h>      // For kern_path, etc.
#include <linux/parser.h>     // For match_* if needed

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Example Author");
MODULE_DESCRIPTION("Clear SB_RDONLY on ext4 filesystem via sysfs");

static char clear_device[64] = "";

/*
 * Helper function to match a super_block by device name.
 * In real usage, you might also compare the mountpoint path or dev_t.
 */
static struct super_block *find_ext4_superblock_by_dev(const char *devname)
{
    struct super_block *sb;

    /*
     * The 'iterate_supers()' approach is recommended;
     * the function you provide will be called for each super_block.
     * For brevity, here we do a simple for_each_super().
     */

    for_each_super(sb) {
        if (!sb->s_bdev) 
            continue;
        if (sb->s_type && strcmp(sb->s_type->name, "ext4") == 0) {
            /* Compare device node name with devname input */
            if (sb->s_bdev->bd_part && sb->s_bdev->bd_part->info) {
                /*
                 * The block device's name might be in sb->s_bdev->bd_disk->disk_name
                 * or via kobject name. There's some nuance when dealing with partitions.
                 */
                if (strcmp(sb->s_bdev->bd_disk->disk_name, devname) == 0) {
                    return sb;
                }
            }
            /*
             * Alternatively, you could compare by major/minor if you parse devname
             * or compare the full path if you do path lookup on devname.
             */
        }
    }

    return NULL;
}

/* 
 * The sysfs "store" callback – called when echoing into /sys/module/clear_sb_ro/parameters/clear_device
 */
static ssize_t clear_device_store(struct kobject *kobj,
                                  struct kobj_attribute *attr,
                                  const char *buf, size_t count)
{
    struct super_block *sb;
    size_t len = min(count, (size_t)(sizeof(clear_device) - 1));

    /* Copy the input string (e.g. "/dev/sda1") and null-terminate */
    strncpy(clear_device, buf, len);
    clear_device[len] = '\0';

    /* Remove trailing newlines, etc. */
    while (len > 0 && (clear_device[len - 1] == '\n' || clear_device[len - 1] == '\r')) {
        clear_device[len - 1] = '\0';
        len--;
    }

    pr_info("clear_sb_ro: requested device=%s\n", clear_device);

    /* Find the superblock for this device */
    sb = find_ext4_superblock_by_dev(clear_device);
    if (!sb) {
        pr_warn("clear_sb_ro: No ext4 sb found for device %s\n", clear_device);
        return count; /* or return an error code */
    }

    /* Clear the read-only flag in memory (risky!). */
    if (sb->s_flags & MS_RDONLY) {
        pr_info("clear_sb_ro: Found ext4 sb on %s, currently MS_RDONLY. Clearing...\n",
                clear_device);

        /* Potentially need additional logic to confirm journaling is consistent. */
        sb->s_flags &= ~MS_RDONLY;

        /* If ext4 also sets internal error state, you might need a direct call to
         * reset ->s_flags or journal error fields in ext4's internal structures.
         */

        pr_info("clear_sb_ro: Completed. Now try remounting read-write in user space.\n");
    } else {
        pr_info("clear_sb_ro: %s superblock not marked read-only.\n", clear_device);
    }

    return count;
}

/*
 * Read callback, if you want to see the current stored device name
 */
static ssize_t clear_device_show(struct kobject *kobj,
                                 struct kobj_attribute *attr,
                                 char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%s\n", clear_device);
}

/* Define sysfs attributes */
static struct kobj_attribute clear_device_attribute =
    __ATTR(clear_device, 0664, clear_device_show, clear_device_store);

static struct attribute *attrs[] = {
    &clear_device_attribute.attr,
    NULL,
};

static struct attribute_group attr_group = {
    .attrs = attrs,
};

static int __init clear_sb_ro_init(void)
{
    int ret;
    struct kobject *kobj;

    /* Usually, we put such attributes under /sys/module/<module_name>/parameters.
     * The simplest approach is module_param() macros, but here we want
     * a custom callback, so we’ll do a kobject approach.
     */
    kobj = kobject_create_and_add("clear_sb_ro", kernel_kobj);
    if (!kobj)
        return -ENOMEM;

    ret = sysfs_create_group(kobj, &attr_group);
    if (ret)
        kobject_put(kobj);

    pr_info("clear_sb_ro: Module loaded. Use /sys/kernel/kobject_test/clear_device\n");
    return ret;
}

static void __exit clear_sb_ro_exit(void)
{
    pr_info("clear_sb_ro: Module unloaded.\n");
    /* Sysfs kobject will be automatically cleaned if created with kobject_create_and_add */
}

module_init(clear_sb_ro_init);
module_exit(clear_sb_ro_exit);
