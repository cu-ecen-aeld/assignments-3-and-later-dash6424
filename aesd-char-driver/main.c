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
#include "linux/slab.h"
#include "linux/string.h"
#include <linux/uaccess.h>
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("DAANISH SHARIFF");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    /* Device info */
    struct aesd_dev *dev;
    PDEBUG("open");
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    /* private data */
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
    ssize_t retval = 0;
    ssize_t offset_bytes_rtn = 0, temp_buffer_count = 0;
    struct aesd_buffer_entry *temp_buffer = NULL;
    struct aesd_dev *dev = filp->private_data;
    if(!dev)
    {
        return -EFAULT;
    }
    if(!buf)
    {
	/* User buffer is NULL. */
        return -EFAULT;
    }

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    /* Aquire mutex */
    if(0 != mutex_lock_interruptible(&aesd_device.lock))
    {
        return -EINTR;
    }
    temp_buffer = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circ_buffer, *f_pos, &offset_bytes_rtn);
    if(temp_buffer == NULL)
    {
        *f_pos = 0;
        goto err_handle;
    }
    if(!(temp_buffer->buffptr))
    {
	goto err_handle;
    }

    /* Temp buffer count  */
    temp_buffer_count = temp_buffer->size - offset_bytes_rtn;
    if(temp_buffer_count < count)
    {
        *f_pos += temp_buffer_count;
    }
    else
    {
        temp_buffer_count = count;
        *f_pos += count;
    }
    /* Copy to user space buffer */
    if(copy_to_user(buf, temp_buffer->buffptr+offset_bytes_rtn, temp_buffer_count))
    {
        retval = -EFAULT;
        goto err_handle;
    }

    retval = temp_buffer_count;
err_handle:
    mutex_unlock(&aesd_device.lock);
    PDEBUG("aesd_read: Return Value %ld", retval);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = NULL;                    // Dev handle
    int complete_flag = 0, actual_size = 0;         // Required elements to determine if complete packet received from user
    struct aesd_buffer_entry aesd_buffer_write;     // Write buffer entry
    struct aesd_buffer_entry *return_buff = NULL;   // Overflown entry
    char *cb_buffer = NULL;                         // Offsets to the dev->cb_buffer
    int i = 0;

    if(!buf)
    {
	return -EFAULT;
    }

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    
    dev = filp->private_data;
    if(!dev)
    {
        return -EFAULT;
    }

    /* Mutex lock */
    if(0 != mutex_lock_interruptible(&aesd_device.lock))
    {
        return -EINTR;
    }

    if(!(dev->cb_size))
    {
        dev->cb_buffer = (char *)kmalloc(count, GFP_KERNEL);
    }
    else
    {
        dev->cb_buffer = (char *)krealloc(dev->cb_buffer, dev->cb_size + count, GFP_KERNEL);
    }
    if(!(dev->cb_buffer))
    {
        retval = -ENOMEM;
        goto err_handle;
    }
    /* Copy userspace buffer to kernel memory */
    cb_buffer = (dev->cb_buffer + dev->cb_size);
    if(copy_from_user(cb_buffer, buf, count))
    {
        retval = -EFAULT;
        goto err_handle;
    }
    for(i = 0; i < count; i++)
    {
        if(cb_buffer[i] == '\n')
        {
            complete_flag = 1;
            actual_size = i+1;
            break;
        }
    }
    if(complete_flag)
    {
        dev->cb_size += actual_size;
        aesd_buffer_write.buffptr = dev->cb_buffer;
        aesd_buffer_write.size = dev->cb_size;
        return_buff = aesd_circular_buffer_add_entry(&(dev->circ_buffer), &aesd_buffer_write);

        /* Update the total buffer size */
        dev->circ_total_size += dev->cb_size;

        /* If circular buffer was overwritten, free the buffer entry that was overwritten */
        if((return_buff) && (return_buff->buffptr) && (dev->circ_buffer.full))
        {
            /* Update the total buffer size */
            dev->circ_total_size -= return_buff->size;

            /* Free the entry buffer */
            kfree(return_buff->buffptr);
            return_buff->buffptr = NULL;
        }
        /* Free the buffer size for next entry */
        dev->cb_size = 0;
    }
    else
    {
        dev->cb_size += count;
    }
    retval = count;
err_handle:
    mutex_unlock(&aesd_device.lock);
    PDEBUG("aesd_write: Return Value %ld", retval);
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    return fixed_size_llseek(filp, off, whence, dev->circ_total_size);
}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long retval = 0;
    int i = 0, fpos = 0;
    /* seek to sructure for user command parsing */
    struct aesd_seekto seek;
    /* Dev handle */
    struct aesd_dev *dev = filp->private_data;

    /* Check if the cmd received is valid for the given aesd kernel driver */
    if((_IOC_TYPE(cmd) != AESD_IOC_MAGIC) || (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) || (AESDCHAR_IOCSEEKTO != cmd))
    {
        /* -ENOTTY is error type for invalid ioctl command */
        return -ENOTTY;
    }

    /* Copy arguments from the user */
    if(0 != copy_from_user(&seek, (const void __user *)arg, sizeof(seek)))
    {
        return -EFAULT;
    }

    /* Check if the write cmd entry is valid */
    if(seek.write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        return -EINVAL;
    }

    /* Mutex lock */
    if(0 != mutex_lock_interruptible(&aesd_device.lock))
    {
        return -EINTR;
    }

    /* Check if the write cmd offset is valid */
    if(seek.write_cmd_offset >= dev->circ_buffer.entry[seek.write_cmd].size)
    {
        retval = -EINVAL;
        goto err_handle;
    }   

    /* Iterate through the circ buff entries and update the offset */
    for(i = 0; i < seek.write_cmd; i++)
    {
        /* Check if entry has no bytes */
        if(0 == dev->circ_buffer.entry[i].size)
        {
            retval = -EINVAL;
            goto err_handle;
        }
        fpos += dev->circ_buffer.entry[i].size;
    }

    /* Unlock mutex */
    mutex_unlock(&aesd_device.lock);

    /* Update the fpos for the current entry aswell */
    fpos += seek.write_cmd_offset;

    /* Update the file position to the offset in filp */
    filp->f_pos = fpos;

    return retval;

err_handle:
    /* Unlock mutex */
    mutex_unlock(&aesd_device.lock);
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .unlocked_ioctl = aesd_unlocked_ioctl,
    .llseek =   aesd_llseek,
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

    /* Init mutex */
    mutex_init(&aesd_device.lock);

    /* Init circular buffer */
    aesd_circular_buffer_init(&(aesd_device.circ_buffer));

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    /* Clean up the Circular buffer */
    struct aesd_buffer_entry *tmp_entry = NULL;
    int idx = 0;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    AESD_CIRCULAR_BUFFER_FOREACH(tmp_entry, &aesd_device.circ_buffer, idx)
    {
      kfree(tmp_entry->buffptr);
      tmp_entry = NULL;
    }
    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
