#ifndef VM_SWAP_H
#define VM_SWAP_H

void swap_init (void);
void swap_in (size_t swap_index, void *kaddr);
size_t swap_out (void *kaddr);
void swap_free (size_t swap_index);
#endif
