CREATE TABLE boards (
  id    INTEGER PRIMARY KEY,
  name  VARCHAR(100) NOT NULL UNIQUE,
  link  VARCHAR(100) NOT NULL UNIQUE,

  code  TEXT NOT NULL,
  notes TEXT NOT NULL,

  created_by INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,

  CONSTRAINT time CHECK (updated_at >= created_at),
  CONSTRAINT creator FOREIGN KEY (created_by)
                     REFERENCES users (id)
                       ON UPDATE CASCADE
                       ON DELETE CASCADE
);

CREATE TABLE users (
  id       INTEGER PRIMARY KEY,
  username VARCHAR(100) NOT NULL UNIQUE,
  name     VARCHAR(100) NOT NULL,
  access   VARCHAR(20)  NOT NULL DEFAULT 'view',
  pwhash   TEXT NOT NULL
);

CREATE TABLE nav (
  owner    INTEGER NOT NULL,
  board    INTEGER NOT NULL,
  position INTEGER NOT NULL DEFAULT 0,

  PRIMARY KEY (owner, board),

  CONSTRAINT owner FOREIGN KEY (owner)
                   REFERENCES users (id)
                     ON UPDATE CASCADE
                     ON DELETE CASCADE,

  CONSTRAINT board FOREIGN KEY (board)
                   REFERENCES boards (id)
                     ON UPDATE CASCADE
                     ON DELETE CASCADE
);

CREATE TABLE sessions (
  sid CHAR(64) NOT NULL PRIMARY KEY,
  uid INTEGER  NOT NULL,

  CONSTRAINT user FOREIGN KEY (uid)
                  REFERENCES users (id)
                    ON UPDATE CASCADE
                    ON DELETE CASCADE
) WITHOUT ROWID;
