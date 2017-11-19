# Bolo Metadata Database

Bolo maintains a SQLite3 database of metadata for the UI.  This is
not used in the metrics gathering or analytics phase; it only
matters to UI users.

## Boards

```sql
CREATE TABLE boards (
  uuid  UUID NOT NULL UNIQUE,
  name  VARCHAR(100) NOT NULL UNIQUE,
  link  VARCHAR(100) NOT NULL UNIQUE,

  code  TEXT NOT NULL,
  notes TEXT NOT NULL,

  created_by UUID NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
)
```

## Users

```sql
CREATE TABLE users (
  uuid     UUID NOT NULL UNIQUE,
  username VARCHAR(100) NOT NULL UNIQUE,
  name     VARCHAR(100) NOT NULL,
  pwhash   VARCHAR(61) NOT NULL,
  role     INTEGER NOT NULL DEFAULT -1
)
```

## Navigation

```
CREATE TABLE nav (
  user     UUID NOT NULL,
  board    UUID NOT NULL,
  position INTEGER NOT NULL DEFAULT 0,

  UNIQUE user_to_board (user, board)
)
```
