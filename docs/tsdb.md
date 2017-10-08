Bolo Time Series Database
=========================

A Bolo data set consists of a single data directory, called the
_data root_, that contains all of the constituent files that store
the time series data.

The top-level file structure is:

```
$dataroot/

  main.db

  idx/
    0000.0000/
      0000.0000.1111.1111.idx
      0000.0000.1111.1112.idx
      ... etc ...
    0000.0001/
      0000.0001.1111.1111.idx
      0000.0001.1111.1112.idx
      ... etc ...

  slabs/
    0000.0000/
      0000.0000.1111.1111.slb
      0000.0000.1111.1112.slb
      ... etc ...
    0000.0001/
      0000.0001.1111.1111.slb
      0000.0001.1111.1112.slb
      ... etc ...
```

`main.db` is a linearized hash that maps each series' name and
canonicalized tag set to the index number.  The name and tag set
are separated by a single `|` character, which is not allowed to
appear in either metric names ore tag names/values.

Each line of `main.db` is formatted thusly:

```
metric|t=a,g=s,e=t\t1002\n
```

The value between the tab character (`\t`) and the (`\n`) is the
ID of the B-tree index, which exists under `idx/`.

`idx/` and `slabs/` house a two-level directory structure for
effectively storing and finding files based on unique 64-bit
unsigned integer IDs.  The top-level directory stores more
sub-directories, each named after the "upper half" of the ID.
Each of these directories, in turn, contain the actual files,
named after the entire ID.  Each _word_ is separated from the next
via a dot.

`idx/` stores the B-tree index files for each and every time
series.  This index maps ranges of time to a unique block contains
the measurements starting at that point in time.  Time is
represented as a 64-bit unsigned integer, in milliseconds, since
January 1st, 1970 00:00:00 UTC, the UNIX epoch.  This gives the
storage format a range of ~500 million years, which should be
sufficient.

`slabs/` stores large files called _slabs_ that each contain up to
2^11 (2,048) _blocks_.  Each block is uniquely numbered, in a
global numbering namespace, and each slab uses the number of its
first block as its ID.  Incidentally, this means that all slab
IDs are multiples of 2,048.

## TSLAB Format

Each slab file consists of a header, followed by 0-2048 blocks
(as detailed in the next section, _TBLOCK Format_).

The header looks like this:

```
     +---------+---------+---------+---------+---------+---------+---------+---------+
   0 | "SLABv1"                                                  | log2(K) | (resv)  |
     +---------+---------+---------+---------+---------+---------+---------+---------+
   8 | ENDIAN CANARY                         | (resv)                                |
     +---------+---------+---------+---------+---------+---------+---------+---------+
  16 | FILE NUMBER                                                                   |
     +---------+---------+---------+---------+---------+---------+---------+---------+
  32 | (resv) (4,008 octets)                                                         |
     \                                                                               \
     /                                                                               /
     |                                                                               |
     +---------+---------+---------+---------+---------+---------+---------+---------+
4032 | HMAC-SHA512 (64 octets)                                                       |
     \                                                                               \
     /                                                                               /
     |                                                                               |
     +---------+---------+---------+---------+---------+---------+---------+---------+
```

Where the constituent fields are defined thusly:

- **MAGIC** - offset 0, length 6 - The literal ASCII code points
  "SLABv1", hex \[53 4c 41 42 76 31\], decimal \[83 76 65 66 118 49\].
- **LOG2(K)** - offset 6, length 1 - The exponent (base 2) of the
  block size of each TBLOCK in this TSLAB.  This **must** be 19,
  since this version of the specifcation only supports 512kb
  TBLOCKS.  This field exists for future expansion.
- **(resv)** - offset 7, length 1 - This octet is reserved for
  use by future versions of this specification.
- **ENDIAN CANARY** - offset 8, length 4 - A canary value that can
  be used to determine the endiannes of the CPU that wrote this
  TSLAB file.  The numeric value `2127639116` will be written, as
  a 32-bit unsigned integer, to offset 8.  On big-endian machines,
  that will translate to hex \[7e d1 32 4c\], whereas
  little-endian CPUs will write hex \[4c 32 d1 7e\].
- **(resv)** - offset 12, length 4 - These four octets are
  reserved for use by future versions of this specification.
- **FILE NUMBER** - offset 16, length 8 - The 64-bit unsigned
  TSLAB ID number.  This is written to the file to allow for
  filesystem recovery where the names of files may be lost.
- **(resv)** - offset 32, length 4,008 - These octets pad out the
  header to a full system page (4k) so that TBLOCK regions will
  align properly, for `mmap()`.  These octets are reserved for
  use by future versions of this specification.
- **HMAC-SHA512** - offset 4,032, length 64 - A SHA-512 HMAC
  digest, calculated over the first 4,032 octets of the header,
  signed with the secret encryption key for the larger data set.
  All `(resv)` fields MUST be zeroed out before the HMAC is
  (re-)calculated.  This HMAC helps to detect TSLAB tampering.

## TBLOCK Format

Inside of each TSLAB are up to 2,048 TBLOCK regions.  Each TBLOCK
is 2^19 octets long (512kb).

The TBLOCK format is:

```
     +--------+--------+--------+--------+--------+--------+--------+--------+
   0 | "BLOKv1"                                            | TUPLE COUNT     |
     +--------+--------+--------+--------+--------+--------+--------+--------+
   8 | BASE TIMESTAMP (ms)                                                   |
     +--------+--------+--------+--------+--------+--------+--------+--------+
  16 | BLOCK NUMBER                                                          |
     +--------+--------+--------+--------+--------+--------+--------+--------+
  24 | MEASUREMENT RELTIME (ms)          | MEASUREMENT VALUE (double)        |
     +--------+--------+--------+--------+--------+--------+--------+--------+
     | MEASUREMENT VALUE (double)        | ...                               |
     +--------+--------+--------+--------+--------+--------+--------+--------+
     | ...                                                                   |
     \                                                                       \
     /                                                                       /
     |                                                                       |
     +--------+--------+--------+--------+--------+--------+--------+--------+
   * | HMAC-SHA512 (64 octets)                                               |
     \                                                                       \
     /                                                                       /
     |                                                                       |
     +--------+--------+--------+--------+--------+--------+--------+--------+
```

Where the constituent fields are defined thusly:

- **MAGIC** - offset 0, length 6 - The literal ASCII code points
  "BLOKv1", hex \[42 4c 4f 4b 76 31\], decimal \[66 76 79 75 118 49\].
- **TUPLE COUNT** - offset 6, length 2 - How many `(ts, value)`
  tuples are present in this block, interpreted as an unsigned,
  16-bit integer.  This limits a single block to holding up to
  64k, or 65,535, measurements.
- **BASE TIMESTAMP** - offset 8, length 8 - The base timestamp, in
  milliseconds since the UNIX epoch, of the earliest possible
  measurement tuple.
- **BLOCK NUMBER** - offset 16, length 8 - The 64-bit unsigned
  TBLOCK ID number.  This is written to the file to allow for
  filesystem recovery where the names of files may be lost.
- **MEASUREMENT RELTIME** - offset 24 + 12n, length 4 - The
  nth measurement tuple's `ts` component, in milliseconds since
  the base timestamp, as a 32-bit unsigned integer.  This puts an
  upper limit of 49.7 days on a single TBLOCK's span in time.
- **MEASUREMENT VALUE** - offset 28 + 12n, length 8 - The nth
  measurement tuple's `value` component.
- **HMAC-SHA512** - offset 524,224 (2^19 - 64), length 64 - A
  SHA-512 HMAC digest, calculated over the first 4,032 octets of
  the header, signed with the secret encryption key for the larger
  data set.  All `(resv)` fields MUST be zeroed out before the
  HMAC is (re-)calculated.  This HMAC helps to detect TBLOCK
  tampering.
