package firehose

import (
	"fmt"
	"io/ioutil"
	"regexp"
	"strconv"

	"github.com/jhunt/go-envirotron"
	"gopkg.in/yaml.v2"
)

type UAAConfig struct {
	Disabled   bool   `yaml:"disabled"    env:"NOZZLE_UAA_DISABLED"`
	URL        string `yaml:"url"         env:"NOZZLE_UAA_URL"`
	Client     string `yaml:"client"      env:"NOZZLE_UAA_CLIENT"`
	Secret     string `yaml:"secret"      env:"NOZZLE_UAA_SECRET"`
	SkipVerify bool   `yaml:"skip_verify" env:"NOZZLE_UAA_SKIP_VERIFY"`
}

type Config struct {
	UAA                  UAAConfig `yaml:"uaa"`
	Prefix               string    `yaml:"prefix"                 env:"NOZZLE_PREFIX"`
	Subscription         string    `yaml:"subscription"           env:"NOZZLE_SUBSCRIPTION"`
	TrafficControllerURL string    `yaml:"traffic_controller_url" env:"NOZZLE_TRAFFIC_CONTROLLER_URL"`
	FlushInterval        string    `yaml:"flush_interval"         env:"NOZZLE_FLUSH_INTERVAL"`
	HighWatermark        string    `yaml:"high_watermark"         env:"NOZZLE_HIGH_WATERMARK"`
	IdleTimeout          string    `yaml:"idle_timeout"           env:"NOZZLE_IDLE_TIMEOUT"`

	highWatermarkBytes   uint64
	flushIntervalSeconds uint64
	idleTimeoutSeconds   uint64
}

func seconds(s string) (uint64, error) {
	if s == "" {
		return 0, nil
	}

	re := regexp.MustCompile("([0-9]+)([HhMmSs])")
	m := re.FindStringSubmatch(s)
	if m == nil {
		return strconv.ParseUint(s, 10, 64)
	}

	v, err := strconv.ParseUint(m[1], 10, 64)
	if err != nil {
		return v, err
	}
	switch m[2] {
	case "H", "h":
		return v * 3600, nil
	case "M", "m":
		return v * 60, nil
	default:
		return v, nil
	}
}

func bytes(s string) (uint64, error) {
	if s == "" {
		return 0, nil
	}

	re := regexp.MustCompile("^([0-9]+)([GgMmKkBb])$")
	m := re.FindStringSubmatch(s)
	if m == nil {
		return strconv.ParseUint(s, 10, 64)
	}

	v, err := strconv.ParseUint(m[1], 10, 64)
	if err != nil {
		return v, err
	}
	switch m[2] {
	case "G", "g":
		return v * 1024 * 1024 * 1024, nil
	case "M", "m":
		return v * 1024 * 1024, nil
	case "K", "k":
		return v * 1024, nil
	default:
		return v, nil
	}
}

func ReadConfig(file string) (*Config, error) {
	c := Config{
		Subscription: "unconfigured-firehose",
		FlushInterval: "60s",
		IdleTimeout:   "5m",
		HighWatermark: "10M",
	}

	if file != "" {
		b, err := ioutil.ReadFile(file)
		if err != nil {
			return nil, fmt.Errorf("failed to read configuration from %s: %s", file, err)
		}

		err = yaml.Unmarshal(b, &c)
		if err != nil {
			return nil, fmt.Errorf("failed to parse configuration from %s: %s", file, err)
		}
	}
	envirotron.Override(&c)
	n, err := seconds(c.FlushInterval)
	if err != nil {
		return &c, err
	}
	c.flushIntervalSeconds = n

	n, err = seconds(c.IdleTimeout)
	if err != nil {
		return &c, err
	}
	c.idleTimeoutSeconds = n

	n, err = bytes(c.HighWatermark)
	if err != nil {
		return &c, err
	}
	c.highWatermarkBytes = n
	return &c, nil
}
