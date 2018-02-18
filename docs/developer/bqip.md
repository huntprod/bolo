Bolo Query Interim Protocol
===========================

BQIP is a compromise intended to get bolo to a working state
sooner, without wasting effort on engineering an HTTP library, a
JSON library, and handling the complexity of the combination.
Instead, the HTTP JSON API will be written in Go, with a
communication to the main bolo system via this interim protocol.

The protocol is a simple request/reply protocol, in which a
connected client issues a BQL query, and the server replies with
either an error or a resultset comprising the data points that the
query represents.

BQIP is 100% textual, represented in 7-bit clean ASCII, using
human-readable quantities for numeric values.

Request Details
---------------

BQIP only defines one request type, the query.  A query looks like
this:

    Q|42|select stuff from ...\n

The `|` character deimits request fields, which are defined
positionally.

The first field is a single character that encodes the request
type.  For now, `Q` is the only valid value (for **Q**uery),

The second field is the length of the query, in base 10 ASCII.
Let's call this quantity `L`.

The third field is exactly `L` octets of data, and is interpreted
as the raw (unparsed) BQL query.  The trailing newline is not
included in the `L` quantity, but is required.

Response Details
----------------

There are only two response types: error and result.  An error
looks like this:

    E|15|something broke\n

Again, the `|` character delimits.

The first field is a single character that encodes the response
type.  `E` signifies an **e**rror.

The second field is the length of the error message, in base 10
ASCII.  Let's call this quantity `L`.

The third field is exactly `L` octets of data, and is interpreted
as a free-form, human readable error message.  The trailing
newline is not included in the `L` quantity, but is required.

If no errors are encountered while processing the request query, a
_result_ response is returned instead.  This is a mildly
complicated encoding, so here's an example:

    R|2\n
    S|14|2498|count=<ts>:<val>,<ts>:<val>...\n
    S|14|2478|name=<ts>:<val>,...\n

This is really three responses in one, but they are bundled.

The first response (`R`) indicates how many resultsets are to be
expected on the line.  The second field is the number of
resultsets that will follow, in base 10 ASCII.  Let's call this
quantity `N`.

The client must read `N` _more_ responses, and they must be type
`S` (set); if any read errors are encountered, or a response type
other than `S` is found, the connection must be terminated, and an
error returned to the caller.

Set results (`S`) contains the actual values, and consist of 4
fields, followed by a newline:

  1. The result type; must be `S`
  2. The number of tuples in the set (base 10 ASCII)
  3. The number of octets used to encode the set tuple list.
  4. The set name and encoded list of set tuple values.

The format of #4 deserves some more discussion.

The first run of characters up to (but not including) the first
`=` sign represents the name of the set, as determined by bolo, or
set explicitly in teh BQL query.  Everything after the first `=`
sign is a comma-separated list of `ts:value` pairs, where:

  - `ts` is the UNIX epoch timestamp for the earliest timestamp in
    the aggregated window, as base 10 ASCII.
  - `value` is the value of the measurement for that time slice,
    encoded in base 10 ASCII in scientific notation.

The scientific notation used is `X.YYYYeZ`, in which `X` can
appear only once, `Y` can appear as many times as necessary to
indicate precision (and may include trailing zeroes), and `Z` can
occur as many times as necessary to indicate powers of 10.  `X`,
`Y`, and `Z` must appear at least once.  Both `X` and `Z`
quantities can be prefixes with a negative sign (`-`).

For example, all of these are valid scientific notation:

  - 1.1e1
  - -1.1e-1
  - 1.1000000000000e1
  - 1.1000000000001e1
  - 1.234567890123456789e5798

The trailing newline is not included the third field of the `S`
response (encoded octet count), but it is required.

Connection Reuse and Pipelining
-------------------------------

If a client has multiple queries to run, they can issue multiple
requests on a single connection, provided that they wait for the
response to the first request before sending the second request.
In other words, pipelining is explicitly not supported.

The ladder diagram looks something like this:

    C ------[ REQUEST ]----> S
        ... time passes ...
      <----[ RESPONSE ]-----
      ------[ REQUEST ]----> S
        ... time passes ...
      <----[ RESPONSE ]-----
    C closes

The client always closes the connection under normal
circumstances.  The server MUST close the connection after receipt
of an invalid request type, or a malformed request, and should
send an error response (`E`) with a suitable diagnostic message
before closing the write end of the connection.

Note that a bad BQL query, whether semantically or syntactically
bad, must result in an error response, but must _not_ cause the
server to close the connection prematurely.

If a client does not understand the response from a server, it
must quietly close the connection, without sending anything
further to the server.
