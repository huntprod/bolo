package main

import (
	"fmt"
	"net"
	"os"
	"strings"
	"time"

	"github.com/jhunt/go-firehose"
)

const (
	NS = 1000000000
)

type Metric struct {
	Timestamp int64
	Value     float64
}

type Nozzle struct {
	Endpoint string
	Tags     string

	renames map[string]string
	metrics map[string]Metric
	values  uint64
}

func NewNozzle(nozzle *Nozzle) *Nozzle {
	if nozzle.Tags == "" {
		nozzle.Tags = "cf=unknown"
	}

	nozzle.renames = make(map[string]string)
	nozzle.Reset()
	return nozzle
}

func (nozzle *Nozzle) Reset() {
	nozzle.metrics = make(map[string]Metric)
}

func (nozzle *Nozzle) Configure(c firehose.Config) {
}

func (nozzle *Nozzle) ingest(k, tags string, ts int64, val float64) {
	if s, ok := nozzle.renames[k]; ok {
		k = s
	} else {
		nozzle.renames[k] = strings.Replace(k, "_", "-", -1)
		k = nozzle.renames[k]
	}
	k = "cf." + k + " " + tags
	nozzle.values += 1
	if v, ok := nozzle.metrics[k]; ok {
		v.Timestamp = ts
		v.Value = val
	} else {
		nozzle.metrics[k] = Metric{
			Timestamp: ts,
			Value:     val,
		}
	}
}

func (nozzle *Nozzle) Track(e firehose.Event) {
	if e.GetOrigin() == "MetronAgent" {
		return
	}

	tags := fmt.Sprintf("%s,sys=%s", nozzle.Tags, e.GetOrigin())
	switch e.Type() {
	case firehose.CounterEvent:
		m := e.GetCounterEvent()
		nozzle.ingest(m.GetName(), tags, e.GetTimestamp(), float64(m.GetTotal()))

	case firehose.ValueMetric:
		m := e.GetValueMetric()
		nozzle.ingest(m.GetName(), tags, e.GetTimestamp(), m.GetValue())

	case firehose.ContainerMetric:
		m := e.GetContainerMetric()
		tags = fmt.Sprintf("%s,app=%s,idx=%d", nozzle.Tags, m.GetApplicationId(), m.GetInstanceIndex())
		nozzle.ingest("app.cpu", tags, e.GetTimestamp(), m.GetCpuPercentage())
		nozzle.ingest("app.mem", tags, e.GetTimestamp(), float64(m.GetMemoryBytes()))
		nozzle.ingest("app.disk", tags, e.GetTimestamp(), float64(m.GetDiskBytes()))
	}
}

func (nozzle *Nozzle) Flush() error {
	now := time.Now().Unix()*NS
	nozzle.ingest("samples",  nozzle.Tags+",origin=nozzle", now, float64(nozzle.values))

	bolo, err := net.Dial("tcp", nozzle.Endpoint)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to connect to %s: %s\n", nozzle.Endpoint, err)
		return nil
	}
	defer bolo.Close()

	for name, metric := range nozzle.metrics {
		fmt.Fprintf(bolo, "%s %d %f\n", name, metric.Timestamp/NS, metric.Value)
	}
	nozzle.Reset()
	return nil
}

func (nozzle *Nozzle) SlowConsumer() {
}
