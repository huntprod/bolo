package main

import (
	"bytes"
	"fmt"
	"io/ioutil"
	"math"
	"net"
	"os"
	"strconv"
	"strings"
)

type Measurement struct {
	Timestamp uint64   `json:"t"`
	Value     *float64 `json:"v"`
}

type Series []Measurement

type Bolo struct {
	Endpoint string
}

func (bolo *Bolo) Plan(q string) ([]string, error) {
	c, err := net.Dial("tcp", bolo.Endpoint)
	if err != nil {
		return nil, err
	}
	defer c.Close()

	_, err = fmt.Fprintf(c, "P|%d|%s\n", len(q), q)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to write P packet to %s: %s\n", bolo.Endpoint, err)
		return nil, fmt.Errorf("an unknown server error has occurred")
	}

	b, err := ioutil.ReadAll(c)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to read result from %s: %s\n", bolo.Endpoint, err)
		return nil, fmt.Errorf("an unknown server error has occurred")
	}
	if len(b) == 0 {
		fmt.Fprintf(os.Stderr, "failed to read result from %s: (no data)\n", bolo.Endpoint)
		return nil, fmt.Errorf("an unknown server error has occurred")
	}

	if b[0] == 'E' {
		fmt.Fprintf(os.Stderr, "%s returned error: %s\n", bolo.Endpoint, string(b[1:]))
		return nil, fmt.Errorf(string(b[1:]))
	}

	if b[0] == 'R' {
		return strings.Split(string(b[2:]), "|"), nil
	}

	fmt.Fprintf(os.Stderr, "unrecognized reply from %\ns[%s]\n[% 02x]\n", bolo.Endpoint, string(b), b)
	return nil, fmt.Errorf("an unknown server error has occurred")
}

func (bolo *Bolo) Query(q string) (map[string]Series, error) {
	c, err := net.Dial("tcp", bolo.Endpoint)
	if err != nil {
		return nil, err
	}
	defer c.Close()

	_, err = fmt.Fprintf(c, "Q|%d|%s\n", len(q), q)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to write Q packet to %s: %s\n", bolo.Endpoint, err)
		return nil, fmt.Errorf("an unknown server error has occurred")
	}

	b, err := ioutil.ReadAll(c)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to read result from %s: %s\n", bolo.Endpoint, err)
		return nil, fmt.Errorf("an unknown server error has occurred")
	}
	if len(b) == 0 {
		fmt.Fprintf(os.Stderr, "failed to read result from %s: (no data)\n", bolo.Endpoint)
		return nil, fmt.Errorf("an unknown server error has occurred")
	}

	if b[0] == 'E' {
		fmt.Fprintf(os.Stderr, "%s returned error: %s\n", bolo.Endpoint, string(b[1:]))
		return nil, fmt.Errorf(string(b[1:]))
	}

	if b[0] == 'R' {
		out := make(map[string]Series)
		rs := bytes.Split(b[1:], []byte("|"))
		for _, r := range rs[1:] {
			l := bytes.Split(r, []byte("="))
			if len(l) != 2 {
				fmt.Fprintf(os.Stderr, "corrupt reply from %s: no metric name found in {%s}\n[%s]\n[% 02x]\n", bolo.Endpoint, string(r), string(b), b)
				return nil, fmt.Errorf("an unknown server error has occurred")
			}

			s := make(Series, 0)
			for _, m := range bytes.Split(l[1], []byte(",")) {
				if len(m) == 0 {
					continue
				}
				tv := bytes.Split(m, []byte(":"))
				if len(tv) != 2 {
					fmt.Fprintf(os.Stderr, "corrupt reply from %s: malformed ts:v tuple found: [%s]\n[%s]\n[% 02x]\n", bolo.Endpoint, string(m), string(b), b)
					return nil, fmt.Errorf("an unknown server error has occurred")
				}

				ts, err := strconv.ParseUint(string(tv[0]), 10, 64)
				if err != nil {
					fmt.Fprintf(os.Stderr, "corrupt reply from %s: %s\n[%s]\n[% 02x]\n", bolo.Endpoint, err, string(b), b)
					return nil, fmt.Errorf("an unknown server error has occurred")
				}

				v, err := strconv.ParseFloat(string(tv[1]), 64)
				if err != nil {
					fmt.Fprintf(os.Stderr, "corrupt reply from %s: %s\n[%s]\n[% 02x]\n", bolo.Endpoint, err, string(b), b)
					return nil, fmt.Errorf("an unknown server error has occurred")
				}

				var vv *float64
				if !math.IsNaN(v) {
					vv = &v
				}
				s = append(s, Measurement{
					Timestamp: ts,
					Value:     vv,
				})
			}

			out[string(l[0])] = s
		}

		return out, nil
	}

	fmt.Fprintf(os.Stderr, "unrecognized reply from %\ns[%s]\n[% 02x]\n", bolo.Endpoint, string(b), b)
	return nil, fmt.Errorf("an unknown server error has occurred")
}
