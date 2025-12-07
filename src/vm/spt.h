#ifndef VM_SPT_H
#define VM_SPT_H

#include <stdbool.h>
#include "threads/palloc.h"
#include "filesys/file.h"
#include <hash.h>
#include <list.h>

enum page_status
{
  PAGE_MEMORY,    // in physical memory
  PAGE_LAZY,      // data to be loaded from file
  PAGE_SWAP,      // data in swap disk
};

struct spt_entry
{
  void *uaddr;                  // user virtual address (key for hash table)
  enum page_status status;      // status of the page

  // for data in physical memory
  struct frame *frame;          // frame the page is loaded into

  // for PAGE_LAZY
  struct file *file;            // file mapped to (if any)
  off_t offset;                 // offset in file
  size_t read_bytes;            // bytes to read from file
  size_t zero_bytes;            // bytes to be zeroed

  // for data in swap
  size_t swap_index;            // index in swap table

  bool writable;                // is page writable?
  struct hash_elem elem;        // for S-page hash table
};

void spt_init (void);
unsigned spt_hash (const struct hash_elem *e, void *aux);
bool spt_less (const struct hash_elem *a, const struct hash_elem *b, void *aux);

// We will always add pages as lazy, then modify status using other helper functions
struct spt_entry* spt_add_lazy_page (struct hash *spt, struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable);
void spt_activate (struct spt_entry *entry, struct frame *frame);

struct spt_entry* spt_lookup (struct hash *spt, void *uaddr);
void spt_print_all (struct hash *spt);
void spt_destroy (struct hash *spt);
#endif
