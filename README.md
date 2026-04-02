This is a repository for Pintos projects, done for the CSED312 course at POSTECH (Fall 2025).
Coding is done up to project 3. For each project, the submission code is organized in branches.
(`1_threads`, `2_userprog`, `3_vm`)


There are a few things to note about our implementation:
- Project 2 is built on top of ONLY the alarm clock functionality of Project 1.
- Project 3 is built on top of the entirety of Project 2.
- In project 2, our implementation for zombie child processes is a bit wonky. In particular,
  when a child process dies before the parent, then instead of the thread being deleted,
  the child process's state stalls in the `sys_exit()` function by a semaphore `zombie_sema`.
  So, the child process's `thread_status` (in `struct thread`) would be in `THREAD_BLOCKED`.
  (This is our implementation of the "zombie" state.)
  - Only when the parent dies will it reap all the zombie children, i.e., increase every children's `zombie_sema`.
    This will let the stalled zombie children to start the dying sequence.
    Also, children that will die in the future will not be stalled by the `zombie_sema` anymore and die safely.
  - A better design would be, if the child dies before the parent,
    to delete the `struct thread` of the children
    while keeping the info of the children process in another (more lightweight) struct.
- In project 3, we don't correctly utilize the "used bits". In particular,
  In our implementation, every frame that was not allocated via `mmap` is sent directly to swap
  as soon as it is selected for eviction, without any additional checks.
  - For frames originally backed by files, such as executable pages, if the dirty bit is not set,
    it would be much more efficient from the perspective of swap-space management
    to simply discard the contents and change the SPTE state to `FILE_LAZY` instead of writing them out to swap.
  - We expect this could be fixed by adding a flag such as `bool file_backed` to `struct spt_entry` and checking that flag right before eviction.

Also, note about the reports: the design reports are very rushed and may contain many errors.
The final reports are much more polished. I tried to store all the core ideas and mechanisms for my code.

Good luck! Also, start early and thank yourself later.
I never started early, and it damaged my mental health a bit..
