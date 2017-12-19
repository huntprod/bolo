package main

import (
	"fmt"
	"os"

	"golang.org/x/crypto/bcrypt"
)

func main() {
	if len(os.Args) == 1 {
		fmt.Fprintf(os.Stderr, "USAGE: bolo-bcrypt PASS [PASS ...]\n")
		os.Exit(1)
	}

	for _, pass := range os.Args[1:] {
		h, err := bcrypt.GenerateFromPassword([]byte(pass), 12)
		if err != nil {
			fmt.Fprintf(os.Stderr, "error: %s\n", err)
			continue
		}
		fmt.Printf("%s\n", string(h))
	}

	os.Exit(0)
}
