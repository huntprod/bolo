The Bolo `process` Collector
============================

The `process` collector lets you zero in on a subset of processes,
and reports various system metrics for a set of processes and
(optionally) their descendant processes.

General Process Information
---------------------------

The most basic information reported by the process collector is
the number of matched processes, and the number of threads of
execution shared across those processes.

    proc.processes  tags=...  1519260319000  1
    proc.threads    tags=...  1519260319000  1

This information can be useful for deriving a per-thread of
per-process value from a composite value.  Be aware that the
process count can be zero (0), if no processes are matched.

Memory Usage
------------

The process collector reports memory (RAM) usage statistics,
bucketing memory ranges into different categories based on their
type:

    proc.mem.libs   tags=...  1519260319000  4206592
    proc.mem.heap   tags=...  1519260319000  3371008
    proc.mem.stack  tags=...  1519260319000  40960
    proc.mem.anon   tags=...  1519260319000  114688
    proc.mem.mmap   tags=...  1519260319000  2211840

The heap, or _free store_, is where most dynamically allocated
memory lives.  When a program calls `malloc(3)` (or a variant),
the standard library searches through the heap and returns a free
block of aproximate size to the caller.  Some times, the heap is
exhausted, or too fragmented to satisfy a request.  In this case,
the standard allocator will extend the heap to encompass more
memory, and allocate out of the new space.  The total size of the
heap is reported in `proc.mem.heap`.

Very few programs running on Linux are statically linked; almost
all of them use some form of dynamic library loading, even if its
just to pull in the standard library.  These shared libraries have
to be mapped into the process address space so that the code can
be executed alongside the text segment of the executable.  The
total size of such mappings, and how much memory they occupy, is
reported in `proc.mem.libs`.  If you ask the process collector to
include measurements for descendent processes, it is smart enough
not too count a lib mapping that is shared across processes twice.
This can happen when a program forks without executing a new
program image.

`proc.mem.stack` reports how much memory space is currently
reserved for use by the program stack.  Stack space general grows
until it hits the peak stack size that the application is expected
to consume, and then holds there.

The total size of memory-mapped regions are reported in
`proc.mem.mmap` (for file-backed mappings) and `proc.mem.anon`
(for anonymous mappings).

Anonymous memory mappings (created by calling `mmap(2)` with the
`MAP_ANONYMOUS` flag) are chunks of memory that are memory-mapped,
but are not backed by any files.  Usually, these are used by
programs that are either doing their own memory allocation outside
of the heap, or by the standard library `malloc(3)` and friends
when they are asked to allocate a chunk too large to satisfy from
heap.

The process collector also reports how much of each type of memory
is currently in swap, using similary named statistics:

    proc.swap.libs   tags=...  1519260319000  0
    proc.swap.heap   tags=...  1519260319000  0
    proc.swap.stack  tags=...  1519260319000  0
    proc.swap.anon   tags=...  1519260319000  0
    proc.swap.mmap   tags=...  1519260319000  0

The types of memory that these statistics represent is the same as
their `proc.mem.*` counterparts.

Measurements are derived from `/proc/<pid>/smaps`.

Virtual Memory Statistics
-------------------------

The process collector reports four virtual memory subsystem
measurements for the process set:

    proc.vm.hwm   tags=...  1519260319000  9949184
    proc.vm.rss   tags=...  1519260319000  9949184
    proc.vm.peak  tags=...  1519260319000  56549376
    proc.vm.size  tags=...  1519260319000  56238080

The first two, `proc.vm.hwm`, and `proc.vm.rss` deal with the
_resident set size_ of the process(es) in question.  `proc.vm.rss`
reports the current size of the regions of process memory that are
currently resident in RAM (not swapped out).  `proc.vm.hwm` is the
highest resident set size seen, since process start.

Similarly, `proc.vm.peak` and `proc.vm.size` deal with the total
amount of virtual memory claimed by the process.  `proc.vm.size`
is the current size, and `proc.vm.peak` is the largest amount of
virtual memory claimed since process start.

Measurements are derived from `/proc/<pid>/status`.

CPU Usage
---------

The process collector reports four CPU usage statistics:

    proc.cpu.utime   tags=... 1519260319000  0.024
    proc.cpu.stime   tags=... 1519260319000  0.014
    proc.cpu.guest   tags=... 1519260319000  0
    proc.cpu.iowait  tags=... 1519260319000  0

`proc.cpu.utime` and `proc.cpu.stime` represent the number of
milliseconds the process set has spent in user mode and kernel
mode, repsectively.

`proc.cpu.guest` measures the time spent operating a virtual CPU
for a virtualized operating system, also reported in milliseconds.
This is generally zero (0).

`proc.cpu.iowait` represents how long (in milliseconds) the
process set has spent waiting on an I/O operation, with no other
work to do.

All of these metrics are cumulative.  Meaningful graphs will
probably want to rate-adjust these into relative quantities.

Measurements are taken from `/proc/<pid>/stat`.

I/O Statistics
--------------

The total number of `read(2)` and `write(2)` system calls is
reported:

    proc.io.reads   tags=... 1519260319000  1285
    proc.io.writes  tags=... 1519260319000  842

Note that these numbers may differ from the number of iops that
are performed on behalf of the process.  Sequential `read(2)`
calls can take advantage of block buffering by satisfying many
reads from a single disk transfer.  Likewise, multiple `write(2)`
calls in quick succession may be coalesced by the kernel into a
single disk operation.

Additionally, the process collector reports the total number of
bytes (octets) read and written by the process:

    proc.io.all.bytes_read      tags=... 1519260319000  1129036
    proc.io.all.bytes_written   tags=... 1519260319000  199454
    proc.io.disk.bytes_read     tags=... 1519260319000  53248
    proc.io.disk.bytes_written  tags=... 1519260319000  57344

The `proc.io.all.*` measurements account for all input/output
operations, including terminals, sockets, etc.  The
`proc.io.disk.*` metrics represent a subset of this, restricted
solely to block devices (usually disks).

Measurements are taken from `/proc/<pid>/io`.

File Descriptor Stats
---------------------

The process collector provides extensive file descriptor usage
statistics under the `proc.fd.*` namespace:

    proc.fd.total    tags=... 1519260319000  5
    proc.fd.block    tags=... 1519260319000  0
    proc.fd.char     tags=... 1519260319000  3
    proc.fd.dir      tags=... 1519260319000  0
    proc.fd.file     tags=... 1519260319000  2
    proc.fd.socket   tags=... 1519260319000  0
    proc.fd.tty      tags=... 1519260319000  0
    proc.fd.unknown  tags=... 1519260319000  0

`proc.fd.total` counts the total number of open file descriptors
for the process set.  The remaining statistics break this number
down into type-specific quantities:

  - `proc.fd.block` - Number of open block devices
  - `proc.fd.char` - Number of open character devices
  - `proc.fd.dir` - Number of open directories
  - `proc.fd.file` - Number of open regular files
  - `proc.fd.socket` - Number of open sockets (UNIX, inet, etc.)
  - `proc.fd.tty` - Number of open terminal devices

The `proc.fd.unknown` statistic is a catch-all, in case the plugin
is unable to properly determine the type of file descriptor.  This
should be zero (0) in all cases, but if it is not, Linux has
either introduced a new file type, or there is a bug in the
process collector.  Either way, the Bolo team would appreciate
hearing from you.

These values are compiled by enumerating all of the directories in
`/proc/<pid>/fd`.
