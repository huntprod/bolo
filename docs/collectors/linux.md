The Bolo `linux` Collector
==========================

The `linux` collector interrogates the Linux kernel and reports
back a varied array of system-level metrics.

Memory and Swap Usage Statistics
--------------------------------

The `mem.*` and `swap.*` metrics that the linux collector reports
let you see how much RAM, and swap space have been allocated to
what purposes.  For single-workload hosts, where the majority of
work will be done by one application, this can often stand in for
more targeted, process-specific memory usage monitoring.

Values are reported in bytes (octets), allowing you to leverage
8-bit byte size formatting in the Bolo UI, without modifying your
queries.

    mem.total     tags=...   1518924563464   6257721344
    mem.used      tags=...   1518924563464   106946560
    mem.free      tags=...   1518924563464   5558300672
    mem.buffers   tags=...   1518924563464   61595648
    mem.cached    tags=...   1518924563464   489758720
    mem.slab      tags=...   1518924563464   41119744

The `mem.total`, `mem.used`, and `mem.free` metrics are
straightforward.

`mem.cached` is the size of the Linux page cache, which is used
for all disk-bound input / output operations.  Whenever the kernel
reads from a block device, it does so by pulling the required
blocks in from the disk, placing them in the page cache, and then
fulfilling the I/O request from memory.  Write operations likewise
update the page cache in-memory, and then mark affected pages as
"dirty" so that they get flushed in the next coalesced sync.

It is normal for `mem.cached` to consume all unused memory; Linux
figures if it's got the memory, it may as well use it.  The page
cache will shrink in response to memory pressure, e.g. from
applications that allocate and access larger blocks of memory.

`mem.buffers` identifies how much memory is being used for buffers
in disk I/O (block) operations.  In modern Linux kernels
(post-2.4), the buffer cache and page cache are unified, since the
pages that represent file content are almost invariably backed by
blocks in a disk block buffer.  What remains in the buffer cache
these days is the metadata about files that are not part of their
content.

It is normal for `mem.buffers` to be small, on the order of tens
of megabytes.

Finally, `mem.slab` is so-called _slab memory_, which is used by
the kernel for its own purposes (process accounting, bookkeeping,
device driver configuration structures, etc.).  This is also often
quite small, since the kernel tries not to hog memory that
user space programs could be using.

It is worth noting that the `mem.used` metric is calculated as:

    mem.total - (mem.free + mem.buffers + mem.cached + mem.slab)

Or, put another way, `mem.used` is the amount of memory used by
user space applications for the day-to-day operation of the host.

swap.total    tags=...   1518924563464   536866816
swap.cached   tags=...   1518924563464   0
swap.used     tags=...   1518924563464   0
swap.free     tags=...   1518924563464   536866816

The `swap.total`, `swap.used` and `swap.free` metrics are also
straightforward.

`swap.cached` reports the amount of swap space that is currently
being used by parts of the page cache.  While it may seem odd to
swap the page cache to disk, which is an I/O operation to a block
device, it can be useful of the access times on the swap device
are significantly better than those of the device that owns the
page (think solid-state vs. spinning rust).

As with the RAM statistics, `swap.used` is actually calculated as:

    swap.total - (swap.free + swap.cached)

and it represents the amount of memory that _applications_ own
that has been swapped to disk.

Memory and swap usage statistics come from `/proc/meminfo`.


System Load Average
-------------------

Most system administrators are familiar with load average, a sort
of _gut-feel_ for how well or how poorly a given host is able to
keep up with the demands placed upon it.

Conventionally, three load averages are calculated, across
different time frames; one minute, five minute, and fifteen
minute.

    load.1min         tags=...   1518924563464   0.02
    load.5min         tags=...   1518924563464   0.04
    load.15min        tags=...   1518924563464   0.00

Historically, the load average has been calculated as a dampened
average of the ratio between runnable processes to schedulable
processes.  A high load average generally indicates that there are
more processes demanding CPU time, which often leads to each such
process getting fewer time slices to execute.  This slows down the
_responsiveness_ or _throughput_ of the system.

Note that load average doesn't take into account `nice(2)` values,
which is why you can still often SSH in to see a load of >100.

In addition to the standard load averages, the linux collector
also reports the number of runnable and schedulable processes:

    load.runnable     tags=...   1518924563464   1
    load.schedulable  tags=...   1518924563464   154

It also reports the number of logical cores, as `load.cpus`, since
load average is not scaled to a per-core metric.  A one-minute
load average of 2 means different things on a single-core VM than
it does on a 32-core rack-mount behemoth.

    load.cpus         tags=...   1518924563464   2

All load.\* metrics except `load.cpus` come from `/proc/loadavg`.
`load.cpu` comes from `/proc/stat`, and is derived by counting
the number of `cpu<#>` lines are seen.


CPU Usage Statistics
--------------------

As the Linux kernel executes on the CPU(s), it keeps track of the
nature and type of work it was doing with each time slice.  The
linux collector taps into these accounting records to report where
the CPU is spending its time executing code.

Note: all of the reported values are measured in _clock ticks_.
Normally, this is defined as 1/100ths of a second.  Also note that
these measurements are cumulative, so you will need to apply some
sort of derivative / rate-calculation function in your BQL queries
to make them useful on a minute-to-minute (or hour-to-hour) basis.

Luckily, in most analysis scenarios, you care about the percentage
of time spent doing one thing and not another, so the use of clock
ticks isn't terribly important.

    cpu.user         tags=...   1518924563464   32547
    cpu.nice         tags=...   1518924563464   0
    cpu.system       tags=...   1518924563464   16847
    cpu.idle         tags=...   1518924563464   15123696
    cpu.iowait       tags=...   1518924563464   506
    cpu.irq          tags=...   1518924563464   0
    cpu.softirq      tags=...   1518924563464   1251
    cpu.steal        tags=...   1518924563464   0
    cpu.guest        tags=...   1518924563464   0
    cpu.guest-nice   tags=...   1518924563464   0

`cpu.user` represents how much time the CPU was executing
instructions form user-space programs.  This is the metric most
people are thinking of when they want the "cpu usage" graphed.

`cpu.nice` also represents CPU executing in user-space, but for
programs that have been _reniced_ to a lower priority.

`cpu.system` is how long the kernel has spent executing its own
code paths, for things like process accounting, device management,
and I/O.

`cpu.idle` counts how long the CPU had nothing constructive to do.
Note that if a CPU is not busy, but the kernel is waiting on an
I/O operation to complete, those clock ticks will be reported as
iowait.

`cpu.iowait` measures how long the CPU was waiting for an I/O
operation to complete.  This has been available since Linux 2.6
(technically 2.5.41).

The `cpu.irq` and `cpu.softirq` metrics indicate how long the
kernel was busy servicing hardware and software interrupt
routines.  These clock ticks are _not_ reported in `cpu.system`.

`cpu.steal` represents time spent in other operating systems,
where this kernel didn't have a chance to run on a core.  High
amounts of stolen time indicate overcrowding on the hypervisor.
This has been available since Linux 2.6.11.

`cpu.guest` measures the amount of time the CPU was busy running
instructions for a guest operating system that is being managed by
this kernel as the hypervisor.  Available since Linux 2.6.24.

Like `cpu.guest`, `cpu.guest-nice` measures time spent in a guest
VM, but only if the VM process itself has been niced to a lower
priority.  Available since Linux 2.6.33.

When you are interpreting these values, it may be worthwhile to
combine the `cpu.user` and `cpu.nice` quantities as "user-space",
the `cpu.system`, `cpu.irq`, and `cpu.softirq` values as
"kernel-space", and lump all the virtualization stats together.

You will also want to (almost always) total all of the values up,
to use as a divisor for each individual value.  That way you deal
with percentages instead of clock ticks.

CPU usage information is derived from `/proc/stat`.


Forking and Kernel/User space Context Switching
----------------------------------------------

The linux collector tracks the total number of `fork(2)` calls
that the kernel has made since start up.  This number,
`context.forks`, represents the total number of processes that
have ever been created, regardless of how long they executed, or
whether they are still alive.

It also tracks the total number of _context switches_ that have
occurred since the kernel booted.  A context switch takes place
any time a user-space process traps into kernel-space by executing a
system call.  Reading from the outside world (standard in, the
network, locally-attached devices, etc.) is the most common cause
of a context switch.  Each context switch pauses the execution of
user-space code and enters kernel mode, and they are not cheap.

    context.forks      tags=...   1518924563464   33967
    context.switches   tags=...   1518924563464   9660619

This information is gleaned from `/proc/stat`.


System-wide Process Statistics
------------------------------

Often, you can detect issues with load and performance by looking
at the number of processes currently resident in the system, and
their execution / runtime state.  The linux collector provides
this snapshot breakdown of 

    procs.total      tags=...   1518924563464   121
    procs.running    tags=...   1518924563464   2
    procs.sleeping   tags=...   1518924563464   119
    procs.blocked    tags=...   1518924563464   0
    procs.zombies    tags=...   1518924563464   0
    procs.stopped    tags=...   1518924563464   0
    procs.unknown    tags=...   1518924563464   0

The total number of processes, regardless of their status, is
reported in `procs.total`.  This metric is calculated as the sum
of all the other `procs.*` metrics; so you can reliably use it in
percentage calculations.

A process is considered to be running if it has work to do on the
CPU, and is not currently blocked waiting for the kernel to do
something.  The total number of running processes is reported in
`procs.running`.

If a process has no work for the CPU to do, i.e. it is waiting on
an alarm(2)-style trigger or waiting for activity on watched file
descriptors, it is considered sleeping.  `procs.sleeping` will
reflect that.

Sometimes, a process is waiting on the kernel to perform some I/O
operation.  These processes are placed in the `procs.blocked`
bucket.  This state is like the `procs.sleeping` state; the key
difference is that sleeping processes can be interrupted, whereas
blocked processes cannot.

The `procs.zombies` metric reports the number of defunct processes
that have not been waited on by their parent processes.  Normally,
this value will be zero, or close to it.  Zombie processes are no
longer executing on the CPU, but their accounting information
(exit status, PID, etc.) are still kept around by the kernel, for
the parent process to see when it finally issues a `wait(2)` or
`waitpid(2)` system call.

Stopped processes are no longer executing because they have been
administratively paused by the operator.  This can happen in
interactive shells with job control when the operator suspends a
job (process).  In bash, this is done via Ctrl-Z.  Debuggers and
other programs that make use of the `ptrace(2)` system call can
also stop processes for their own purposes.  The number of
currently stopped processes is reported in `procs.stopped`.  This
value is also usually zero or close to it.

Your particular flavor of Linux may feature process states that
are not handled by this plugin.  For example, older Linux kernels
(2.6 and below) have a state for processes that are currently
involved in a page operation.  Any state not handled by the linux
collector gets lumped into the `procs.unknown` metric.

These metrics are gathered from the process-specific
`/proc/$PID/stat` files.


System-wide File Descriptor Statistics
--------------------------------------

File descriptors are a limited resource in the Linux kernel.  Most
workloads won't ever hit the upper limit on file descriptors.
However, monitoring is concerned with misbehaving applications in
abnormal workloads.  All it takes is one file descriptor leaked
per network client on a busy server to exhaust the pool of
available file descriptors.

The linux collector tracks file descriptor usage system-wide.

    openfiles.used   tags=...   1518924563465   864
    openfiles.max    tags=...   1518924563465   605542

The `openfiles.max` metric is a system-wide kernel maximum on the
number of allocable descriptors.  Generally, this value is so much
higher than the used open files that you often won't want to graph
the two series together.

It's also worth pointing out that each process has its own, much
lower limit on the number of open file descriptors.  It is common
place for an application to exhaust its own limit before the
system itself runs out of descriptors.

This information is retrieved from the Linux _sysctl_ subsystem,
via the `/proc/sys/fs/file-nr` file.

Also note that prior to Linux 2.6, the kernel allocated file
descriptors dynamically, but did not deallocate them when they
were no longer used.  Instead, "freed" file descriptors were
placed in a free list, and the kernel would report the size of
that free list.  Since modern Linux kernels no longer do this, the
linux collector does not report an `openfiles.free` metric.


Filesystem Statistics
---------------------

Disks filling up are responsible for more outages than we as a
progression care to admit.  The linux collector helps you out on
this front by collecting metrics about each mounted filesystem.

Raw byte capacity metrics are reported under the `fs.bytes.*`
namespace:

    fs.bytes.total    ...,path=...,dev=...   1518924563465   19966849024
    fs.bytes.free     ...,path=...,dev=...   1518924563465   7259926528
    fs.bytes.rfree    ...,path=...,dev=...   1518924563465   1037877248

`fs.bytes.total` reports the full capacity, in bytes, of the
mounted filesystem.  The amount of unallocated space is given by
`fs.bytes.free`.  You can calculate the usage in graphs by
subtracting the two quantities.

Almost every filesystem reserves some of its block for superuser
use.  This helps administrators (acting as the root user) to deal
with "full disk" scenarios by giving them scratch space to work.
You can see how much of this reserved space is free via the
`fs.bytes.rfree` metric.

There's more to filesystems than just raw byte capacity.  It is
not unusual for a filesystem to be "full" but still have blocks
available.  This happens when the filesystem has run out of _inode
entries_.

The linux collector returns inodes metrics as well:

    fs.inodes.total   ...,path=...,dev=...   1518924563465   1248480
    fs.inodes.free    ...,path=...,dev=...   1518924563465   878307
    fs.inodes.rfree   ...,path=...,dev=...   1518924563465   0

As with their bytes-based counterparts, `fs.inodes.total` tracks
the total number of inode entries available in the filesystem, and
`fs.inodes.free` reports the number of unused entries.

Likewise, the `fs.inodes.rfree` metric tracks the number of inodes
that are reserved for use by the superuser.

This information is derived from the `/proc/mounts` file.

If the host has multiple block devices mounted as filesystems, the
linux collector will emit one complete set of metrics for each,
adding the `path=...` and `dev=...` tags to identify which
mountpoint (path) and backing device (dev) are being analyzed.


Virtual Memory Management Statistics
------------------------------------

The virtual memory subsystem is an integral part of the Linux
kernel, and monitoring its activities, health, and performance is
vital.

    vm.pgpgin          tags=...   1518924563466   437862
    vm.pgpgout         tags=...   1518924563466   1034238
    vm.pswpin          tags=...   1518924563466   0
    vm.pswpout         tags=...   1518924563466   0
    vm.pgfree          tags=...   1518924563466   18729881
    vm.pgfault         tags=...   1518924563466   18828811
    vm.pgmajfault      tags=...   1518924563466   2125
    vm.pgsteal         tags=...   1518924563466   0
    vm.pgscan.kswapd   tags=...   1518924563466   0
    vm.pgscan.direct   tags=...   1518924563466   0

`vm.pgpgin` and `vm.pgpgout` track the total number of pages that
have been paged into memory, or out to disk, respectively, by the
VMM.  `vm.pswpin` and `vm.pswpout` do likewise, for pages that are
swapped in from / out to swap space.  These are counters, which
only reset when the kernel starts up.

The `vm.pgfree` stat indicates how many pages have been freed by
the VMM, since kernel boot.

`vm.pgfault` tracks the number of minor page faults since kernel
start up, and `vm.pgmajfault` tracks how many major page faults
have occurred.

The `vm.pgsteal` metric aggregates the per-zone page steals that
the kernel reports.  A _page steal_ occurs when the virtual memory
subsystem "steals" a page from the cache to satisfy a memory
request from an application.  An uptick in page steals indicates
an increase in memory pressure leading to the eviction of cache.

Periodically, the Linux VMM scans all pages in the cache to see
which can be reclaimed to keep the number of free pages at an
acceptable level.  The `vm.pgscan.kswapd` stat tracks how many
pages where freed by a scan in the `kswapd` kernel thread, while
`vm.pgscan.direct` tracks how many were directly scanned by the
kernel.  These values are reset at kernel boot.

All of the virtual memory management metrics are derived from
`/proc/vmstat`.


Block Device I/O Statistics
---------------------------

The linux collector tracks disk input / output activity inside the
kernel, and reports via the `diskio.*` hierarchy.

    diskio.read-iops    ...,dev=...   1518924563466   11267
    diskio.read-miops   ...,dev=...   1518924563466   5555
    diskio.write-iops   ...,dev=...   1518924563466   39889
    diskio.write-miops  ...,dev=...   1518924563466   51382

All input / output is done in terms of `iops`, short for I/O
operations.  The raw number of read and write iops is reported in
`diskio.read-iops` and `diskio.write-iops`.  The exact size of an
iop is unspecified, but each operation is bounded.

Modern kernels try to optimize the relatively slow bits of I/O by
coalescing or merging adjacent iops together, into larger and more
time-efficient operations.  For example, if an application issues
a series of `write(2)` calls to subsequent byte ranges in a file,
the kernel may merge all of them into a single write operation
working on the entire, aggregate range.  These are called _merged
iops_, and they are reported in the `diskio.read-miops`, and
`diskio.write-miops` metrics.

Note that, by definition, the number of merged iops will always be
less than the number of discrete iops.

Since the size of a given iop isn't standardized, the linux
collector uses separate metrics to report the total number of
bytes read from / written to each disk device:

    diskio.read-bytes   ...,dev=...   1518924563466   448174080
    diskio.write-bytes  ...,dev=...   1518924563466   1059059712

The linux collector also tracks how many milliseconds were spent
performing read and write iops:

    diskio.read-msec    ...,dev=...   1518924563466   7804
    diskio.write-msec   ...,dev=...   1518924563466   84036

All values are cumulative since kernel boot.

If there are multiple disk devices (i.e. different logical
volumes, software RAID devices, or physical disk partitions), the
linux collector will emit a full set of metrics for each, setting
the `dev=...` tag appropriately on each.


Network Interface / Traffic Statistics
--------------------------------------

In the modern, connected world, a computer without a network is no
computer at all.  Ideally, we'd like to monitor the ingress and
egress traffic on our hosts, and the linux collector makes that
happen.

For analyzing throughput and traffic, the linux collector provides
the following metrics:

    net.rx.bytes        ...,iface=eth0   1518924563466   26890047
    net.rx.packets      ...,iface=eth0   1518924563466   277151
    net.tx.bytes        ...,iface=eth0   1518924563466   23096191
    net.tx.packets      ...,iface=eth0   1518924563466   221203

This is the pattern for network metric names.

  1. They all live under the `net.*` namespace.
  2. Ingress (receive) stats are under `net.rx.*`
  3. Egress (transmit) stats are under `net.tx.*`

You get both raw bytes (octets) sent under `net.*.bytes`, as well
as packet counts via `net.*.packets`.  These values are cumulative
to the last time the interface statistics were reset, which
normally only happens at boot.  These values are subject to
rollover.

You can calculate a median packet size series by dividing raw
bytes by the packet count.  This should generally only be done on
rate-adjusted values; otherwise you end up with a lifetime median,
which isn't terribly useful.

In addition, the collector separates out certain types of packets
that are received / transmitted, based on their type:

    net.rx.compressed   ...,iface=eth0   1518924563466   0
    net.tx.compressed   ...,iface=eth0   1518924563466   0
    net.rx.multicast    ...,iface=eth0   1518924563466   0

`net.rx.compressed` and `net.tx.compressed` count the number of
compressed packets that have been received or sent (respectively)
since kernel boot.  `net.rx.multicast` identifies how many
multicast packets have been received, also since kernel boot.

The linux collector also reports various types of inbound errors:

    net.rx.errors       ...,iface=eth0   1518924563466   0
    net.rx.drops        ...,iface=eth0   1518924563466   0
    net.rx.overruns     ...,iface=eth0   1518924563466   0
    net.rx.frames       ...,iface=eth0   1518924563466   0

`net.rx.errors` indicates the total number of receive errors, as
detected by the driver, since kernel start up.

The `net.rx.drops` metric counts the number of packets that have
been received by the driver but dropped by the kernel for one
reason or another.  Dropped packets never make it to the kernel,
and therefore will never make it to user-space applications.

The `net.rx.overruns` metric is the number of FIFO buffer overruns
that have been encountered by the interface driver.

Finally, `net.rx.frames` counts how many framing errors have been
detected in received packets.

Similar errors exist for outbound packet flow:

    net.tx.errors       ...,iface=eth0   1518924563466   0
    net.tx.drops        ...,iface=eth0   1518924563466   0
    net.tx.overruns     ...,iface=eth0   1518924563466   0
    net.tx.collisions   ...,iface=eth0   1518924563466   0
    net.tx.carrier      ...,iface=eth0   1518924563466   0

The first three are the outbound (transmit) variations of their
`net.rx.*` counterparts.

`net.tx.collisions` indicates how many media collisions have been
detected by the interface driver.  Collisions may indicate an
overloaded physical link layer, or a bad broadcast situation.

The `net.tx.carrier` metric counts up the number of times the
interface driver has detected carrier (signal) loss.

Note: for each interface on the system, the linux collector will
report a complete set of metrics, adding the `iface=...` tag to
differentiate one interface from another.
