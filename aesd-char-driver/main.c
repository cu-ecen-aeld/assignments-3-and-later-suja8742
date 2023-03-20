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
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Sudarshan Jagannathan"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev; /* Pointer to the aesd_dev struct elements*/ */
    
    PDEBUG("open");
    
	dev = container_of(inode->i_cdev, struct aesd_dev, cdev); /* The address of the container structure of cdev is returned and stored in dev  */
	filp->private_data = dev; /* The open syscall sets this pointer to NULL before calling the open method to the driver. By assigning addr, other modules can use it.*/

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
    size_t offset_pos;
    struct aesd_dev *dev = filp->private_data;
    int kernel_buff_count = 0;
    struct aesd_buffer_entry *kernel_buff;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */

     //Mutex required
     mutex_lock(&aesd_device.lock_prim);
    kernel_buff = aesd_circular_buffer_find_entry_offset_for_fpos(&device->aesd_circular_buffer, *f_pos, &offset_pos); //In the temp kernel buff, we store the address of next location to read. 

    if(kernel_buff == NULL)
    {
        *f_pos = 0;
        goto clean; //goto is not frowned upon in kernel programming. We use it to perform necessary cleanup steps. 
    }

    if((kernel_buff->size - offset_pos) < count) //count
    {
        *f_pos += (kernel_buff->size - offset_pos);
        kernel_buff_count = (kernel_buff->size - offset_pos); //?
    }

    else
    {
        *f_pos += count;
        kernel_buff_count = count;
    }

    if(copy_to_user(buf, kernel_buff->buffptr + offset_pos, kernel_buff_count))
    {
        retval = -EFAULT;
        goto clean;
    }
    
    retval = kernel_buff_count;

    clean: mutex_unlock(&aesd_device.lock_prim);

    PDEBUG("Return Value %ld", retval);

    
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;   //Return value for the function
    char *tmp_buff;
    int string_end_flag;
    int tmp_iterator = 0;
    int i; //For loop iterator
    int temp_counter = 0; //Variable to hold the temporarily increased size based on complete/incomplte strings. 
    struct aesd_dev *dev;
    struct aesd_buffer_entry aesd_buffer_write_entry;   //aesd_buffer_entry value of the struct. 
    char *ret_ptr;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */

    dev = filp->private_data;

    mutex_lock(&aesd_device.lock_prim);
    tmp_buff = (char *)kmalloc(count, GFP_KERNEL); //May sleep, use mutex or semaphore. 

    if(tmp_buff == NULL)
    {
        retval = -ENOMEM;
        goto clean_exit;
    }   

    if(copy_from_user(tmp_buff, buf, count))    //Copy from the userspace buf into tmp_buff
    {
        retval = -EFAULT;
        goto clean_exit;
    }

    for(i=0; i < count; i++)
    {
        if(tmp_buff[i] == '\n')
        {
            string_end_flag = 1;    //Indicates written string is complete. 
            tmp_iterator = i+1;     //Store the value of the index for '\n'
            break;
        }
    }

    if(dev->buffer_size == 0)
    {
        dev->copy_buffer_ptr = (char *)kmalloc(count, GFP_KERNEL);
        if(dev->copy_buffer_ptr == NULL)
        {
            retval = -ENOMEM;
            goto free;
        }
        memcpy(dev->copy_buffer_ptr, tmp_buff, count);
        dev->buffer_size +=count;
    }

    else
    {
        //Handling partial write
        if(string_end_flag)
        {
            temp_counter = tmp_iterator;
        }

        else
        {
            temp_counter = count;
        }

        //Reallocate the copy buffer size based on temporary incrementing of the size variable. 
        dev->copy_buffer_ptr = (char*)krealloc(dev->copy_buffer_ptr, dev->buffer_size + temp_counter, GFP_KERNEL);

        if(dev->copy_buffer_ptr == NULL)
        {
            retval = -ENOMEM;
            goto free;
        }

        memcpy(dev->copy_buffer_ptr + dev->buffer_size, tmp_buff, temp_counter);
        dev->buffer_size += temp_counter;
    }

    if(string_end_flag)
    {
        //Move the data onto the circular buffer

        aesd_buffer_write_entry.buffptr = dev->copy_buffer_ptr;
        aesd_buffer_write_entry.size = dev->buffer_size;
        
    

    ret_ptr = aesd_circular_buffer_add_entry(&dev->aesd_circular_buffer, &aesd_buffer_write_entry);

    if(ret_ptr != NULL)
    {
        kfree(ret_ptr);
    }

    dev->buffer_size = 0;

    }


retval = count;

free: kfree(tmp_buff);
clean_exit: mutex_unlock(&aesd_device.lock_prim);


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

     mutex_init(&aesd_device.lock_prim);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{

    uint8_t index;
    struct aesd_buffer_entry *entry;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

     AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.aesd_circular_buffer, index)
     {
        kfree(entry->buffptr);
     }


    mutex_destroy(&aesd_device.lock_prim);
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
