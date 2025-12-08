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

  // for PAGE_MEMORY
  struct frame *frame;          // frame the page is loaded into

  // for PAGE_LAZY
  struct file *file;            // file mapped to (if any)
  off_t offset;                 // offset in file
  size_t read_bytes;            // bytes to read from file
  size_t zero_bytes;            // bytes to be zeroed

  // for PAGE_SWAP
  size_t swap_index;            // index in swap table

  bool mmap;                    // is this page memory-mapped?
  bool writable;                // is page writable?
  struct hash_elem elem;        // for S-page hash table
};

void spt_init (void);
unsigned spt_hash (const struct hash_elem *e, void *aux);
bool spt_less (const struct hash_elem *a, const struct hash_elem *b, void *aux);

/* Every spt entry creation is done via one of these functions */
struct spt_entry* spt_add_lazy_page (struct hash *spt, struct file *file, off_t ofs, uint8_t *uaddr, uint32_t read_bytes, uint32_t zero_bytes, bool writable);
bool spt_map_file_to_lazy (struct hash *spt, struct file *file, off_t start_ofs, uint8_t *start_uaddr, size_t total_read_bytes, size_t total_zero_bytes, bool writable, bool mark_mmap);

/* Helper functions to change states of pages. The programmer need only take care of the high-level logic, using these functions */
void spt_memory_to_swap (struct spt_entry *entry);
void spt_memory_to_lazy (struct spt_entry *entry);
bool spt_swap_to_memory (struct spt_entry *entry);
bool spt_lazy_to_memory (struct spt_entry *entry);

bool spt_initialize_as_memory (void *upage);
// bool spt_initialize_as_lazy ();

struct spt_entry* spt_lookup (struct hash *spt, void *uaddr);
void spt_print_all (struct hash *spt);
void spt_destroy_entry (struct hash_elem *e, void *aux);

#endif
