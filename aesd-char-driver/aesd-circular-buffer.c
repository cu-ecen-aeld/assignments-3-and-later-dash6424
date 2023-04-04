/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

/*==========================================================================
   INCLUDES
========================================================================== */
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/*==========================================================================
   MACROS
========================================================================== */
#define TRUE 1
#define FALSE 0

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
    /* total bytes store the overall size of all the entries.
     * tmp_offset decremental offset to find the offset within an entry */
    size_t total_bytes = 0, tmp_offset = char_offset;

    /* read offset points to oldest buffer in the queue*/
    uint8_t read_offset = buffer->out_offs, i = 0;

    /* NULL check */
    if((!buffer) || (!entry_offset_byte_rtn))
    {
        return NULL;
    }

    /* Iterate through the CB queue and find the required entry and offset */
    for(i=0; i<AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++)
    {
        total_bytes += buffer->entry[read_offset].size;

        /* If char offset found in given entry, break. */
        if(char_offset < total_bytes)
        {
            break;
        }

        /* Decrement temp offset by each entry's size */
        tmp_offset -= buffer->entry[read_offset].size;

        /* if read overflows, reset to 0 */
        read_offset++;
        if(read_offset == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {
            read_offset = 0;
        }

    }
    /* Check if total_bytes are valid and char_offset is in bound */
    if((total_bytes < 0) || (char_offset >= total_bytes))
    {
        *entry_offset_byte_rtn = 0;
        return NULL;
    }

    /* Actual entry number based on the read offset */
    *entry_offset_byte_rtn = tmp_offset;

    /* Return the actual buffer entry */
    return &(buffer->entry[read_offset]);
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
struct aesd_buffer_entry *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    struct aesd_buffer_entry *res = NULL;

    /* NULL check for pointers */
    if((!buffer) || (!add_entry))
    {
        return res;
    }

    /* If buffer is full, increment read pointer */
    if(buffer->full)
    {
        /* Update the overwritten entry to be returned */
        res = &(buffer->entry[buffer->out_offs]);
        buffer->out_offs++;
        if(buffer->out_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {
            buffer->out_offs = 0;
        }
    }

    /* Populate the entry buffer irrespective of buffer is full or not */
    buffer->entry[buffer->in_offs].buffptr = add_entry->buffptr;
    buffer->entry[buffer->in_offs++].size = add_entry->size;
    if(buffer->in_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        buffer->in_offs = 0;
    }

    /* Check if last entry and set full flag */
    if((!(buffer->full)) && (buffer->in_offs == buffer->out_offs))
    {
        buffer->full = TRUE;
    }
    return res;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
