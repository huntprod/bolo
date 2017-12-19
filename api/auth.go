package main

import (
	"golang.org/x/crypto/bcrypt"
)

func (u *User) Authenticate(password string) bool {
	return bcrypt.CompareHashAndPassword(
		[]byte(u.PasswordHash),
		[]byte(password)) == nil
}
