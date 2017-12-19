package main

import (
	"fmt"
	"strings"
	"time"

	"github.com/jhunt/go-db"
	_ "github.com/mattn/go-sqlite3"
)

type DB struct {
	c *db.DB
}

func Database(path string) (*DB, error) {
	d := &DB{
		c: &db.DB{
			Driver: "sqlite3",
			DSN:    path,
		},
	}

	return d, d.c.Connect()
}

func (d *DB) lastID(id *int) error {
	r, err := d.c.Query(`SELECT last_insert_rowid()`)
	if err != nil {
		return err
	}

	defer r.Close()
	if !r.Next() {
		return fmt.Errorf("unable to determine inserted id")
	}

	return r.Scan(&id)
}

func (d *DB) Insert(thing interface{}) error {
	switch thing.(type) {
	default:
		return fmt.Errorf("unrecognized object type (%v)", thing)

	case *Board:
		x := thing.(*Board)
		now := time.Now().Unix()
		if x.Name == "" {
			return fmt.Errorf("board objects require a name")
		}
		if x.Link == "" {
			return fmt.Errorf("board objects require a link")
		}
		if x.Owner == nil {
			return fmt.Errorf("board objects require an owner")
		}
		err := d.c.Exec(`INSERT INTO boards
		           (name, link, code, notes, created_by, created_at, updated_at)
		    VALUES (?,    ?,    ?,    ?,     ?,          ?,          ?)`,
			x.Name, x.Link, x.Code, x.Notes, x.Owner.ID, now, now)
		if err != nil {
			return err
		}
		return d.lastID(&x.ID)

	case *User:
		x := thing.(*User)
		if x.Username == "" {
			return fmt.Errorf("user objects require a username")
		}
		if x.Name == "" {
			return fmt.Errorf("user objects require a name")
		}
		if x.Access == "" {
			x.Access = WriteAccess
		}
		return d.c.Exec(`INSERT INTO users (username, name, access, pwhash) VALUES (?, ?, ?)`,
			x.Username, x.Name, x.Access, x.PasswordHash)
		return d.lastID(&x.ID)

	case *Nav:
		x := thing.(*Nav)
		if x.Owner == nil {
			return fmt.Errorf("nav objects require an owner")
		}
		if x.Board == nil {
			return fmt.Errorf("nav objects require a board")
		}
		return d.c.Exec(`INSERT INTO nav (owner, board, position) VALUES (?, ?, ?)`,
			x.Owner.ID, x.Board.ID, x.Position)

	case *Session:
		x := thing.(*Session)
		if x.SID == "" {
			return fmt.Errorf("session objects require a session id")
		}
		if x.User == nil {
			return fmt.Errorf("session objects require a user object")
		}
		return d.c.Exec(`INSERT INTO sessions (sid, uid) VALUES (?, ?)`,
			x.SID, x.User.ID)
	}
}

func (d *DB) Update(thing interface{}) error {
	switch thing.(type) {
	default:
		return fmt.Errorf("unrecognized object type (%v)", thing)

	case *Board:
		x := thing.(*Board)
		now := time.Now().Unix()
		if x.ID == 0 {
			return fmt.Errorf("board objects without IDs cannot be updated")
		}
		if x.Name == "" {
			return fmt.Errorf("board objects require a name")
		}
		if x.Link == "" {
			return fmt.Errorf("board objects require a link")
		}
		if x.Owner == nil {
			return fmt.Errorf("board objects require an owner")
		}
		return d.c.Exec(`UPDATE boards SET
		    name = ?, code = ?, notes = ?, updated_at = ?
		    WHERE id = ? AND link = ?`,
			x.Name, x.Code, x.Notes, now,
			x.ID, x.Link)

	case *User:
		x := thing.(*User)
		if x.Username == "" {
			return fmt.Errorf("user objects require a username")
		}
		if x.Name == "" {
			return fmt.Errorf("user objects require a name")
		}
		if x.Access == "" {
			x.Access = WriteAccess
		}
		return d.c.Exec(`UPDATE users SET name = ?, access = ?, pwhash = ? WHERE username = ?`,
			x.Name, x.Access, x.PasswordHash, x.Username)

	case *Nav:
		return fmt.Errorf("nav objects can only be created or deleted")

	case *Session:
		return fmt.Errorf("session objects can only be created or deleted")
	}
}

func (d *DB) Delete(thing interface{}) error {
	switch thing.(type) {
	default:
		return fmt.Errorf("unrecognized object type (%v)", thing)

	case *Board:
		x := thing.(*Board)
		if x.ID == 0 {
			return fmt.Errorf("board objects without IDs cannot be deleted")
		}
		if x.Link == "" {
			return fmt.Errorf("board objects without links cannot be deleted")
		}
		return d.c.Exec(`DELETE FROM boards WHERE id = ? AND link = ?`,
			x.ID, x.Link)

	case *User:
		x := thing.(*User)
		if x.Username == "" {
			return fmt.Errorf("user objects require a username")
		}
		return d.c.Exec(`DELETE FROM users WHERE username = ?`,
			x.Username)

	case *Nav:
		x := thing.(*Nav)
		if x.Owner == nil {
			return fmt.Errorf("nav objects require an owner")
		}
		if x.Board == nil {
			return fmt.Errorf("nav objects require a board")
		}
		return d.c.Exec(`DELETE FROM nav WHERE owner = ? AND board = ?`,
			x.Owner.ID, x.Board.ID)

	case *Session:
		x := thing.(*Session)
		if x.SID == "" {
			return fmt.Errorf("session objects require a session id")
		}
		if x.User == nil {
			return fmt.Errorf("session objects require a user object")
		}
		return d.c.Exec(`DELETE FROM sessions WHERE sid = ? AND uid = ?`,
			x.SID, x.User.ID)
	}
}

func (d *DB) GetBoard(id int, link string) (*Board, error) {
	if id == 0 && link == "" {
		return nil, fmt.Errorf("GetBoard() requires either an id or a link, or both")
	}

	where := ""
	args := make([]interface{}, 0)

	if link == "" {
		where = `b.id = ?`
		args = append(args, id)
	} else if id == 0 {
		where = `b.link = ?`
		args = append(args, link)
	} else {
		where = `b.id = ? AND b.link = ?`
		args = append(args, id)
		args = append(args, link)
	}

	r, err := d.c.Query(`
	    SELECT
	      b.id, b.name, b.link, b.code, b.notes, b.created_at, b.updated_at,
	      u.id, u.username, u.name, u.access, u.pwhash
	    FROM boards b INNER JOIN users u
	      ON b.created_by = u.id
	    WHERE `+where, args...)
	if err != nil {
		return nil, err
	}

	defer r.Close()
	if !r.Next() {
		return nil, nil
	}

	b := &Board{
		Owner: &User{},
	}
	if err := r.Scan(&b.ID, &b.Name, &b.Link, &b.Code, &b.Notes, &b.CreatedAt, &b.UpdatedAt,
		&b.Owner.ID, &b.Owner.Username, &b.Owner.Name, &b.Owner.Access, &b.Owner.PasswordHash); err != nil {
		return nil, err
	}

	return b, nil
}

func (d *DB) GetBoards() ([]*Board, error) {
	r, err := d.c.Query(`
	    SELECT
	      b.id, b.name, b.link, b.code, b.notes, b.created_at, b.updated_at,
	      u.id, u.username, u.name, u.access, u.pwhash
	    FROM boards b INNER JOIN users u
	      ON b.created_by = u.id`)
	if err != nil {
		return nil, err
	}

	defer r.Close()
	bb := make([]*Board, 0)
	for r.Next() {
		b := &Board{
			Owner: &User{},
		}
		if err := r.Scan(&b.ID, &b.Name, &b.Link, &b.Code, &b.Notes, &b.CreatedAt, &b.UpdatedAt,
			&b.Owner.ID, &b.Owner.Username, &b.Owner.Name, &b.Owner.Access, &b.Owner.PasswordHash); err != nil {
			return nil, err
		}
		bb = append(bb, b)
	}

	return bb, nil
}

func (d *DB) GetBoardsFor(user *User) ([]*Board, error) {
	if user == nil || user.ID == 0 {
		return nil, fmt.Errorf("GetBoardsFor() requires a valid user object, with an id")
	}

	r, err := d.c.Query(`
	    SELECT b.name, b.link, b.code
	    FROM nav n INNER JOIN boards b ON n.board = b.id
	               INNER JOIN users  u ON n.owner = u.id
	    WHERE u.id = ?
	    ORDER BY n.position ASC`, user.ID)
	if err != nil {
		return nil, err
	}

	defer r.Close()
	bb := make([]*Board, 0)
	for r.Next() {
		b := &Board{}
		if err := r.Scan(&b.Name, &b.Link, &b.Code); err != nil {
			return nil, err
		}
		bb = append(bb, b)
	}
	return bb, nil
}

func (d *DB) GetUser(id int, username string) (*User, error) {
	if id == 0 && username == "" {
		return nil, fmt.Errorf("GetUser() requires either an id or a username, or both")
	}

	where := ""
	args := make([]interface{}, 0)

	if id == 0 {
		where = `u.username = ?`
		args = append(args, username)
	} else if username == "" {
		where = `u.id = ?`
		args = append(args, id)
	} else {
		where = `u.id = ? AND u.username = ?`
		args = append(args, id)
		args = append(args, username)
	}

	r, err := d.c.Query(`
	    SELECT
	      u.id, u.username, u.name, u.access, u.pwhash
	    FROM users u
	    WHERE `+where, args...)
	if err != nil {
		return nil, err
	}

	defer r.Close()
	if !r.Next() {
		return nil, nil
	}

	u := &User{}
	if err := r.Scan(&u.ID, &u.Username, &u.Name, &u.Access, &u.PasswordHash); err != nil {
		return nil, err
	}

	return u, nil
}

func (d *DB) GetSession(sid string) (*Session, error) {
	if sid == "" {
		return nil, fmt.Errorf("GetSession() requires a session id")
	}

	r, err := d.c.Query(`
	    SELECT
	      s.sid,
	      u.id, u.username, u.name, u.access, u.pwhash
	    FROM sessions s INNER JOIN users u
	      ON s.uid = u.id
	    WHERE sid = ?`, sid)
	if err != nil {
		return nil, err
	}

	defer r.Close()
	if !r.Next() {
		return nil, nil
	}

	s := &Session{
		User: &User{},
	}
	if err := r.Scan(&s.SID,
		&s.User.ID, &s.User.Username, &s.User.Name, &s.User.Access, &s.User.PasswordHash); err != nil {
		return nil, err
	}

	return s, nil
}

func (d *DB) SetNavs(user *User, nn []string) error {
	if user == nil || user.ID == 0 {
		return fmt.Errorf("SetNavs() requires a valid user object, with an id")
	}
	if len(nn) == 0 {
		return fmt.Errorf("SetNavs() requires at least one board to be selected")
	}

	if err := d.c.Exec(`DELETE FROM nav WHERE owner = ?`, user.ID); err != nil {
		return err
	}

	ii := make([]interface{}, len(nn))
	for i := range nn {
		ii[i] = nn[i]
	}

	in := "link IN (?" + strings.Repeat(", ?", len(nn)-1) + ")"
	r, err := d.c.Query(`SELECT id, link FROM boards WHERE `+in, ii...)
	if err != nil {
		return err
	}

	defer r.Close()
	boards := make(map[string]int)
	for r.Next() {
		var (
			id   int
			link string
		)
		if err := r.Scan(&id, &link); err != nil {
			return err
		}
		boards[link] = id
	}

	for pos, n := range nn {
		if err := d.c.Exec(`INSERT INTO nav (board, owner, position) VALUES (?, ?, ?)`,
			boards[n], user.ID, pos); err != nil {
			return err
		}
	}

	return nil
}
