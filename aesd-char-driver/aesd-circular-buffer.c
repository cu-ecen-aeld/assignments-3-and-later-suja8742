/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */


#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"


/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    /**
    * TODO: implement per description
    */

    uint8_t index_readptr; //Used as a local var equivalent of the read pointer in the structure. 
    int counter = 0;       //Checks how many iterations are going through the loop

    index_readptr = buffer->out_offs;

    if(buffer == NULL)
    {
        return NULL;
    }

    if(entry_offset_byte_rtn == NULL)
    {
        return NULL;
    }

    while( (char_offset >= 0) ) //Check for the offset. 
    {

        if(counter == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)  //This indicates that the total offset size is greater than the total size of the buffer. 
        {
            return NULL;
        }

        if(index_readptr == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {
            index_readptr = 0;
        }

        if(char_offset > (buffer->entry[index_readptr].size) - 1)   //Reduce the offset by index_size
        {
            char_offset = (char_offset - buffer->entry[index_readptr].size);
            index_readptr++;
        }

        else if(char_offset == buffer->entry[index_readptr].size)   //offset is the same as size. 
        {
            *entry_offset_byte_rtn = (buffer->entry[index_readptr].size - 1);  
            char_offset = 0;
            break;  //Update and exit the loop
        }

        else
        {
            *entry_offset_byte_rtn = char_offset;
            char_offset = 0;
            break;
        }
        
        counter++;
    }

    return &buffer->entry[index_readptr];
    

}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
char *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    /**
    * TODO: implement per description
    */

    char *return_ptr = NULL;
    if(buffer == NULL)
    {
        return NULL;
    }

    if(add_entry == NULL)
    {
        return NULL;
    }

    if(buffer->full)
    {
        return_ptr = (char *)buffer->entry[buffer->out_offs].buffptr;
        buffer->out_offs++;

        if(buffer->out_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {
            buffer->out_offs = 0;
        }
    }

    /* Instering entry on write pointer */
    buffer->entry[buffer->in_offs].buffptr = add_entry->buffptr;
    buffer->entry[buffer->in_offs].size = add_entry->size;
    buffer->in_offs++;

    /* Handling the situations after the first roll-arounds.  */

    if(buffer->in_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        buffer->in_offs = 0;
    }



    if((buffer->out_offs == buffer->in_offs) && (!buffer->full))
    {
        buffer->full = 1;
    }
    return return_ptr;

}
/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
