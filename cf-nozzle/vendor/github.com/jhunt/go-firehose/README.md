go-firehose - A Cloud Foundry Firehose Platform
===============================================

![Travis CI](https://travis-ci.org/jhunt/go-firehose.svg?branch=master)

Writing [Firehose Nozzles][1] for Cloud Foundry should be easier
than it is.  This framework aims to make that want a reality.

Usage is (by design) straightforward.  Import the library, and
then implement the `firehose.MyNozzle` interface, and then pass
an instance of that object to `firehose.Go`

Here's a small example that just prints stuff to standard output:

```
package main

import (
  "fmt"

  "github.com/cloudfoundry/sonde-go/events"
  "github.com/jhunt/go-firehose"
)

type MyNozzle struct{}

func (MyNozzle) Configure(c firehose.Config) {
  fmt.Printf("Configure() was called!\n")
}

func (MyNozzle) Track(e *events.Envelope) {
  fmt.Printf("received a %s message from %s\n", e.GetEventType(), e.GetOrigin())
}

func (MyNozzle) Flush() error {
  fmt.Printf("Flush() was called!\n")
  return nil
}

func (MyNozzle) SlowConsumer() {
  fmt.Printf("we are not keeping up with the firehose!\n")
}

func main() {
  firehose.Go(MyNozzle{}, "/etc/firehose.yml")
}
```

(more examples can be found in the `examples/` directory.  They
are all buildable Go applications).



Caveats
-------

This library uses the Go standard `log` library for printing
messages it thinks you need to see.  If you don't like this
approach, find a different library.  Pull requests to add JSON
logging to this library will be closed without further
explanation.


[1]: https://docs.cloudfoundry.org/loggregator/nozzle-tutorial.html
