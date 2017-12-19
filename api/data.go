package main

const (
	ViewAccess  = "view"
	WriteAccess = "write"
)

type Board struct {
	ID   int
	Name string
	Link string

	Code  string
	Notes string

	Owner     *User
	CreatedAt int
	UpdatedAt int
}

type User struct {
	ID           int
	Username     string
	Name         string
	Access       string
	PasswordHash string
}

type Nav struct {
	Owner    *User
	Board    *Board
	Position int
}

type Session struct {
	SID  string
	User *User
}
