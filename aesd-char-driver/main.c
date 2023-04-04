
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
#include <linux/fs.h> 
#include "aesdchar.h"
#include "linux/slab.h"
#include "linux/string.h"
#include "aesd_ioctl.h"



int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Sudarshan Jagannathan"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev; /* Pointer to the aesd_dev struct*/
    
    PDEBUG("open");
    
	dev = container_of(inode->i_cdev, struct aesd_dev, cdev);   //The address of the container structure where cdev is located is returned and stored in dev
	filp->private_data = dev; //THe open syscall sets filp to NULL before calling the open method to the driver. By assigning addr, other modules can use it. 
    /**
     * TODO: handle open
     */
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
    int kernel_buffer_count = 0;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *kernel_buff;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    mutex_lock(&aesd_device.lock);  //Kernel lock primitive

    kernel_buff = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->aesd_circular_buffer, *f_pos, &offset_pos);

    if (kernel_buff == NULL) //if address was not returned. 
    {      
        *f_pos = 0; 
        goto clean; //goto is not frowned upon in kernel programming, use it to jump to cleanup steps. 
    }
    
    if ((kernel_buff->size - offset_pos) < count) 
    {
        *f_pos += (kernel_buff->size - offset_pos);    //Based on the returned offset and size, update f_pos. 
        kernel_buffer_count = kernel_buff->size - offset_pos;
    } 
    
    else 
    {
        *f_pos += count;
        kernel_buffer_count = count;
    }

    if (copy_to_user(buf, kernel_buff->buffptr+offset_pos, kernel_buffer_count)) //copying to buf, which is a userspace buffer.
    {      
		retval = -EFAULT;
		goto clean;
	}

    retval = kernel_buffer_count;     
    /**
     * TODO: handle read
     */
    clean: mutex_unlock(&aesd_device.lock);

    PDEBUG("Return Value %ld", retval);

    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0; // Return Value
    
    char *temp_buffer; 
    int i; // Loop iterator
    int string_end_flag = 0; 
    int temp_iterator = 0; 
    int temp_counter = 0;
    struct aesd_buffer_entry aesd_buffer_write_entry; 
    struct aesd_dev *dev;



    char *ret_ptr;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    dev = filp->private_data;

    

   
    temp_buffer = (char *)kmalloc(count, GFP_KERNEL);   

    if (temp_buffer == NULL) 
    {
        retval = -ENOMEM;
        goto exit_clean;
    }
    mutex_lock(&aesd_device.lock);  //kmalloc may sleep, use mutex to lock. 

    // Copying into the kernel buffer from buf
    if (copy_from_user(temp_buffer, buf, count)) {
        retval = -EFAULT;
		goto free;
	}

    // Iterating over bytes received to check for "\n" character
    for (i = 0; i < count; i++) {
        if (temp_buffer[i] == '\n') {
            string_end_flag = 1; // Setting packet complete flag to indicate that string is complete when \n is received. 
            temp_iterator = i+1; // Setting temp_iterator value to store by indicating null character loc
            break;
        }
    }

    // Check if copy buffer size is 0; mallocing and copying local buffer into global buffer
    if (dev->buffer_size == 0) {
        dev->copy_buffer_ptr = (char *)kmalloc(count, GFP_KERNEL);
        if (dev->copy_buffer_ptr == NULL) {
            retval = -ENOMEM;
            goto free;
        }
        memcpy(dev->copy_buffer_ptr, temp_buffer, count);
        dev->buffer_size += count;
    } 
    else {
        
        // Case when write command is issued without '\n', append next received chars to it. 
        if (string_end_flag)
        {
            temp_counter = temp_iterator;
        }
        else
        {
            temp_counter = count;
        }

        // Reallocate copy buffer size based on temporary size increment variable

        dev->copy_buffer_ptr = (char *)krealloc(dev->copy_buffer_ptr, dev->buffer_size + temp_counter, GFP_KERNEL);
        if (dev->copy_buffer_ptr == NULL) 
        {
            retval = -ENOMEM;
            goto free;
        }

        // Copying temp_buffer contents into copy_buffer
        memcpy(dev->copy_buffer_ptr + dev->buffer_size, temp_buffer, temp_counter);
        dev->buffer_size += temp_counter;        
    }
    
    // Adding entry onto circular buffer if packet is complete
    if (string_end_flag) 
    {

        // Adding entry onto circular buffer
        aesd_buffer_write_entry.buffptr = dev->copy_buffer_ptr;
        aesd_buffer_write_entry.size = dev->buffer_size;
        ret_ptr = aesd_circular_buffer_add_entry(&dev->aesd_circular_buffer, &aesd_buffer_write_entry);
    
        // Freeing return_pointer if buffer is full 
        if (ret_ptr != NULL)
            kfree(ret_ptr);
        
        dev->buffer_size = 0;
    } 

    retval = count;

    /**
     * TODO: handle write
     */
    free: kfree(temp_buffer);
    exit_clean: mutex_unlock(&aesd_device.lock);
    return retval;
}

static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset) { //P)arams and return based on the lecture
   
   struct aesd_dev *dev = filp->private_data;

    long ret_val = 0;

    int fpos = 0;    //Local variable to update the filp->f_pos

    int i = 0;  //Loop iterator

    //Check for valid write_cmd and write_cmd offset values

    //Return error if the command has not been written as of yet. 

    if(write_cmd > (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - 1))   //Out of range write_cmd
    {
        //Return -EINVAL if write command is out of range
        return -EINVAL;

    }

    if(write_cmd_offset >= dev->aesd_circular_buffer.entry[write_cmd].size) //Error case  where offset is >= size of command
    {
        //Return -EINVAL if the write command offset is out of range
        return -EINVAL;
    }

    mutex_lock(&aesd_device.lock);

    for(i=0; i<write_cmd; i++) {
        //iterate to write_cmd entry in the buffer

        if(!dev->aesd_circular_buffer.entry[i].size)    //If the size of any entry is 0, return out and free the lock.
        {
            ret_val = -EINVAL; //Invalid value
            goto clean;
        }

        fpos += dev->aesd_circular_buffer.entry[i].size;    //Update fpos based on the size of each entry.
    }
    
    fpos += write_cmd_offset;   //Add the relative offset after looping to the appropriate write_cmd.

    filp->f_pos = fpos;     //Update the filp structure f_pos member for access. 

    clean: mutex_unlock(&aesd_device.lock);
    return ret_val;

}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {

    long ret_val = 0;
    struct aesd_seekto seekto;

    //Reference: Linux Device Drivers textbook

    if(_IOC_TYPE(cmd) != AESD_IOC_MAGIC)    //Supported Magic number is 0x16. Error check to verify that. 
    {
        return -ENOTTY;     //"No typewriter" - Error code for invalid ioctl
    }

    if(_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR)   //Bounds check - Maximum number of commands allowed. 
    {
        return -ENOTTY; 
    }

    //Implemented based on lecture slides.
    switch(cmd) {

        case AESDCHAR_IOCSEEKTO:
        
            
            if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) != 0) {
                ret_val = EFAULT;
            } else {
               aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset); //adjust file offset
            }
            break;
        default:
            ret_val = -ENOTTY;;
            //ERROR default
            break;
            
    }

    return ret_val;

}

loff_t aesd_llseek_custom_imp(struct file *filp, loff_t offset_ab, int whence)
{
    //Option 2 for custom llseek_custom implementation as described in lectures.
    //Parameters modified based on man llseek. The filp, abs offset value, result to return, and whence, to support SEEK_CUR, SEEK_SET, SEEK_END.

    loff_t result_retval = 0;

    struct aesd_dev *dev;
    dev = filp->private_data;

    mutex_lock(&aesd_device.lock);  //Kernel lock used to implement fixed_size_llseek

    //Using fixed_size_llseek for logic as mentioned in option 2.
    result_retval = fixed_size_llseek(filp, offset_ab, whence, get_current_buffer_size(&dev->aesd_circular_buffer));    

    mutex_unlock(&aesd_device.lock);

    return result_retval;
    

}



struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek_custom_imp,
    .unlocked_ioctl = aesd_ioctl,
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

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.aesd_circular_buffer, index) {
      kfree(entry->buffptr);
    }
    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}




module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
