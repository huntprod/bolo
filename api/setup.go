package main

import (
	"fmt"
)

func (d *DB) Setup() error {
	var err error

	err = d.c.Exec(`
CREATE TABLE IF NOT EXISTS users (
  id       INTEGER PRIMARY KEY,
  username VARCHAR(100) NOT NULL UNIQUE,
  name     VARCHAR(100) NOT NULL,
  access   VARCHAR(20)  NOT NULL DEFAULT 'view',
  pwhash   TEXT NOT NULL
)`)
	if err != nil {
		return fmt.Errorf("failed to create missing 'users' table: %s", err)
	}

	err = d.c.Exec(`
CREATE TABLE IF NOT EXISTS boards (
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
)`)
	if err != nil {
		return fmt.Errorf("failed to create missing 'boards' table: %s", err)
	}

	err = d.c.Exec(`
CREATE TABLE IF NOT EXISTS nav (
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
)`)
	if err != nil {
		return fmt.Errorf("failed to create missing 'nav' table: %s", err)
	}

	err = d.c.Exec(`
CREATE TABLE IF NOT EXISTS sessions (
  sid CHAR(64) NOT NULL PRIMARY KEY,
  uid INTEGER  NOT NULL,

  CONSTRAINT user FOREIGN KEY (uid)
                  REFERENCES users (id)
                    ON UPDATE CASCADE
                    ON DELETE CASCADE
) WITHOUT ROWID`)
	if err != nil {
		return fmt.Errorf("failed to create missing 'sessions' table: %s", err)
	}

	if err = d.CreateFailsafeUser(); err != nil {
		return err
	}
	if err = d.CreateDefaultBoard(); err != nil {
		return err
	}

	return nil
}

func (d *DB) CreateFailsafeUser() error {
	r, err := d.c.Query(`SELECT id FROM users LIMIT 1`)
	if err != nil {
		return fmt.Errorf("failed to determine if we should create a failsafe user: %s", err)
	}
	defer r.Close()

	if r.Next() {
		return nil
	}

	err = d.c.Exec(`
INSERT INTO users (username, name, access, pwhash)
           VALUES ('bolo', 'Administrator', 'write', '$2a$12$01hSbXDva6NWaY5FeRkJFusTf8WxHFZxNuIylwYKNqe7VSBSUlbVS');
`)
	if err != nil {
		return fmt.Errorf("failed to create failsafe user: %s", err)
	}

	return nil
}

func (d *DB) CreateDefaultBoard() error {
	r, err := d.c.Query(`SELECT id FROM boards LIMIT 1`)
	if err != nil {
		return fmt.Errorf("failed to determine if we should create a default board: %s", err)
	}
	defer r.Close()

	if r.Next() {
		return nil
	}

	err = d.c.Exec(`
INSERT INTO boards (name, link, notes, created_by, created_at, updated_at, code)
  VALUES ("Home", "home", "The Default Bolo Dashboard", 1, 1513723634, 1513723634,
    'placeholder { text "Hello, Bolo!" }'
  )`)
	if err != nil {
		return fmt.Errorf("failed to create default board: %s", err)
	}

	return nil
}
