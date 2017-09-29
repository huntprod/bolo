Bolo Time Series Database
=========================

A Bolo data set consists of a single data directory, called the
_data root_ that contains all of the constituent files that store
the time series data.

The top-level file structure is:

```
$dataroot/

  main.db

  ts/
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

  tags/
    host.tbl
    env.tbl
    platform.tbl
    ... etc ...
```
