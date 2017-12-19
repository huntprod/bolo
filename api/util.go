package main

import (
	"encoding/json"
	"io/ioutil"
	"net/http"
	"fmt"
	"os"
)

func readJSON(dst interface{}, r *http.Request) error {
	b, err := ioutil.ReadAll(r.Body)
	if err != nil {
		return err
	}
	return json.Unmarshal(b, dst)
}

func respondWith(w http.ResponseWriter, code int, thing interface{}) {
	b, err := json.Marshal(thing)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to marshal json response: %s\n", err)
		w.WriteHeader(500)
		fmt.Fprintf(w, `{"error":"an internal json error has occurred"}`)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	w.Write(b)
}

func respond(w http.ResponseWriter, code int, err string) {
	respondWith(w, code, struct {
		Error string `json:"error"`
	}{
		Error: err,
	})
}

func badRequest(w http.ResponseWriter, err string) {
	respond(w, 400, err)
}

func authRequired(w http.ResponseWriter, err string) {
	respond(w, 401, err)
}

func accessDenied(w http.ResponseWriter, err string) {
	respond(w, 403, err)
}
