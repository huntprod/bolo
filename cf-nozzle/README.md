## Cloud Foundry Firehose Nozzle for Bolo

This repository contains the code for a Cloud Foundry Firehose
Nozzle that forwards Metrics and Events to a Bolo aggregator.

To use it, you either need to provide a configuration file, which
looks like this:

```
---
uaa:
  url:         https://your-uaa-url
  client:      your-uaa-client-id
  secret:      your-uaa-secret
  skip_verify: no                        # or yes

traffic_controller_url: wss://your-doppler-url
subscription:           bolo-nozzle      # or something else...
prefix:                'cf:sandbox:'     # or something else...

flush_interval: 20s
```

Or define the following environment variables:

- `NOZZLE_UAA_URL` - The URL of your UAA instance
- `NOZZLE_UAA_CLIENT` - UAA Client ID for authentication
- `NOZZLE_UAA_SECRET` - UAA Client Secret for authentication
- `NOZZLE_UAA_SKIP_VERIFY` - Set to yes/y/true/1 to skip SSL verification
   (expiration and name) of the UAA endpoint.  **Not recommended for
   production use.**
- `NOZZLE_TRAFFIC_CONTROLLER_URL` - The URL of the Loggregator / Doppler
   websockets endpoint.
- `NOZZLE_PREFIX` - A string to prepend to each metric name, before
   submitting to Bolo.
- `NOZZLE_SUBSCRIPTION` - The subscription ID to register with the firehose.
   Multiple instances of this nozzle, using the same subscription ID, will
   split the firehose stream amongst them, roughly evenly.
- `NOZZLE_FLUSH_INTERVAL` - How often should metrics be sent to Bolo?
- `BOLO_ENDPOINT` - The ip:port of the bolo to stream metrics into
- `BOLO_TAGS` - A comma-separated list of k=v tags to apply to all
  metrics that are submitted.

Happy Hacking!
