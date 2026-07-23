/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Min Min Thein"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    //filp->private_data = &aesd_device; 
    struct aesd_dev *dev; /* device information */
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset = 0;
    size_t bytes_available;
    size_t bytes_to_copy;
    size_t bytes_copied = 0;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

   

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(
                &dev->buffer, *f_pos, &entry_offset);
    if (entry == NULL)
    {
        retval = 0;   // EOF: no data available at this offset
        goto out;
    }

    do
    {   
        bytes_available = entry->size - entry_offset;
        bytes_to_copy = min(count - bytes_copied, bytes_available);

        if (copy_to_user(buf + bytes_copied, entry->buffptr + entry_offset, bytes_to_copy))
        {
            retval = -EFAULT;
            goto out;
        }

        bytes_copied += bytes_to_copy;
        *f_pos += bytes_to_copy;
        entry_offset = 0; // Reset entry_offset for subsequent entries
        entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset);
        
    } while (entry != NULL && bytes_copied < count);
    
    retval = bytes_copied;
    
out:
    mutex_unlock(&dev->lock);   
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = filp->private_data;
    char *tmp;
    char *newline_pos;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    tmp = krealloc(dev->buffer_partial, dev->size_partial + count, GFP_KERNEL);
    if (!tmp)
    {
        retval = -ENOMEM;
        goto out;
    }
    dev->buffer_partial = tmp;
    if (copy_from_user(dev->buffer_partial + dev->size_partial, buf, count))
    {
        retval = -EFAULT;
        goto out;
    }
    dev->size_partial += count;
    retval = count;

    /* Check for newline */
    while ((newline_pos = memchr(dev->buffer_partial, '\n', dev->size_partial)) != NULL)
    {
        size_t line_len = (newline_pos - dev->buffer_partial) + 1;
        size_t remainder_len = dev->size_partial - line_len;
        struct aesd_buffer_entry new_entry;

        new_entry.buffptr = kmalloc(line_len, GFP_KERNEL);
        if (!new_entry.buffptr)
        {
            retval = -ENOMEM;
            goto out;
        }
        memcpy(new_entry.buffptr, dev->buffer_partial, line_len);
        new_entry.size = line_len;

        if (dev->buffer.full)
            kfree(dev->buffer.entry[dev->buffer.in_offs].buffptr);
        aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);

        if (remainder_len > 0)
        {
            char *remainder = kmalloc(remainder_len, GFP_KERNEL);
            if (!remainder) { retval = -ENOMEM; goto out; }
            memcpy(remainder, dev->buffer_partial + line_len, remainder_len);
            kfree(dev->buffer_partial);
            dev->buffer_partial = remainder;
            dev->size_partial = remainder_len;
        }
        else
        {
            kfree(dev->buffer_partial);
            dev->buffer_partial = NULL;
            dev->size_partial = 0;
        }
    }

    

out:
    mutex_unlock(&dev->lock);   
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.buffer);
    aesd_device.buffer_partial = NULL;
    aesd_device.size_partial = 0;

    
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        if (aesd_device.buffer.entry[i].buffptr) {
            kfree(aesd_device.buffer.entry[i].buffptr);
            aesd_device.buffer.entry[i].buffptr = NULL;
        }
    }

    if (aesd_device.buffer_partial) {
        kfree(aesd_device.buffer_partial);
        aesd_device.buffer_partial = NULL;
    }
    mutex_destroy(&aesd_device.lock);
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
