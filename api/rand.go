package main

import (
	"crypto/rand"
)

var alpha = []rune("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKMNOPQRSTUVWXYZ0123456789")

func randomString(n int) string {
	bb := make([]byte, n)
	if x, err := rand.Reader.Read(bb); err != nil || x != n {
		return ""
	}
	for i, b := range bb {
		bb[i] = byte(alpha[int(b)%len(alpha)])
	}
	return string(bb)
}
