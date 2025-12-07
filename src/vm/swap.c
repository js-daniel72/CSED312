#include "vm/swap.h"
#include <bitmap.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "devices/block.h"

static struct block *swap_device;
static size_t swap_total_pages;
static struct bitmap *swap_bitmap;
static struct lock swap_lock;

void
swap_init (void)
{
  swap_device = block_get_role (BLOCK_SWAP);
  if (swap_device == NULL)
    PANIC ("No swap device found, can't initialize swap.");

  swap_total_pages = block_size (swap_device) / PGSIZE;
  swap_bitmap = bitmap_create (swap_total_pages);
  if (swap_bitmap == NULL)
    PANIC ("No memory for swap bitmap, can't initialize swap.");

  lock_init (&swap_lock);
}

void
swap_in (size_t swap_index, void *kaddr)
{
  lock_acquire (&swap_lock);
  if (!bitmap_test (swap_bitmap, swap_index))
    PANIC ("swap_in: trying to swap in from a free swap slot.");
  for (size_t i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++)
    {
      block_read (swap_device, swap_index * (PGSIZE / BLOCK_SECTOR_SIZE) + i,
                  kaddr + i * BLOCK_SECTOR_SIZE);
    }
  bitmap_set (swap_bitmap, swap_index, false);
  lock_release (&swap_lock);
}

size_t
swap_out (void *kaddr)
{
  lock_acquire (&swap_lock);
  size_t swap_index = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);
  if (swap_index == BITMAP_ERROR)
    PANIC ("swap_out: no free swap slots available.");
  for (size_t i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++)
    {
      block_write (swap_device, swap_index * (PGSIZE / BLOCK_SECTOR_SIZE) + i,
                   kaddr + i * BLOCK_SECTOR_SIZE);
    }
  lock_release (&swap_lock);
  return swap_index;
}

void
swap_free (size_t swap_index)
{
  lock_acquire (&swap_lock);
  if (!bitmap_test (swap_bitmap, swap_index))
    PANIC ("swap_free: trying to free a free swap slot.");
  bitmap_set (swap_bitmap, swap_index, false);
  lock_release (&swap_lock);
}
