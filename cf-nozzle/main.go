package main

import (
	fmt "github.com/jhunt/go-ansi"
	"os"
	"strings"

	"github.com/jhunt/go-cli"
	env "github.com/jhunt/go-envirotron"
	"github.com/jhunt/go-firehose"
)

func main() {
	var opts struct {
		Config   string   `cli:"-c, --config"`
		Endpoint string   `cli:"-e, --endpoint" env:"BOLO_ENDPOINT"`
		Tags     []string `cli:"-t, --tags"     env:"BOLO_TAGS"`
	}
	opts.Config = "cf-bolo-nozzle.yml"
	env.Override(&opts)

	_, _, err := cli.Parse(&opts)
	if err != nil {
		fmt.Fprintf(os.Stderr, "@R{!!! %s}\n", err)
		os.Exit(1)
	}

	firehose.Go(NewNozzle(&Nozzle{
		Endpoint: opts.Endpoint,
		Tags:     strings.Join(opts.Tags, ","),
	}), opts.Config)
}
