# Bolo HTTP REST API

## Authentication

### POST /v1/auth

**Request**:

```
POST /v1/auth HTTP/1.1
Accept: application/json
Content-Type: application/json
Content-Length: ...

{
  "username": "foo",
  "password": "bar"
}
```

**Response**:

```
HTTP/1.1 403 Forbidden
Content-Type: application/json
Content-Length: ...

{
  "error": "Invalid username or password."
}
```

or

```
HTTP/1.1 200 OK
Location: /
Set-Cookie: bolo1=MDpbsiQcpXFh2bOs1ar0KfxSlmChFWfYzGcIsaAUeg; Expires=Mon, 20 Nov 2017 11:00:00 GMT
Content-Type: application/json
Content-Length: ...

{
  "ok"  : "Authenticated successfully.",
  "sid" : "MDpbsiQcpXFh2bOs1ar0KfxSlmChFWfYzGcIsaAUeg"
}
```

### DELETE /v1/auth

**Request**:

```
DELETE /v1/auth
Cookie: bolo1=MDpbsiQcpXFh2bOs1ar0KfxSlmChFWfYzGcIsaAUeg
```

or

```
DELETE /v1/auth
X-Bolo-Session: MDpbsiQcpXFh2bOs1ar0KfxSlmChFWfYzGcIsaAUeg
```

**Response**:

```
HTTP/1.1 204 No Content
```

## Navigation

### GET /v1/nav

**Request**:

```
GET /v1/nav HTTP/1.1
Accept: application/json
```

**Response**:

```
HTTP/1.1 200 OK
Content-Type: application/json
Content-Length: ...

[
  {
    "name": "Home",
    "link": "/boards/home"
  }
]
```

### PUT /v1/nav

**Request**:

```
POST /v1/nav HTTP/1.1
Content-Type: application/json
Content-Length: ...

[
  "home",
  "other-board"
]
```

**Response**:

```
HTTP/1.1 204 No Content
```


## Dashboard Management

### GET /v1/boards

**Request**:

```
GET /v1/boards HTTP/1.1
Accept: application/json
```

**Response**:

```
HTTP/1.1 200 OK
Content-Type: application/json
Content-Length: ...

[
  {
    "name"  : "Home",
    "href"  : "/boards/home",
    "notes" : "..."
  }
]
```

Note: code is not present in the /v1/boards listing payload, a
performance optimization aimed at eliminating the need for
pagination in the API.

An optional query string paramter of `link` can be passed to limit
the search to a single board, based on an exact match with its
link attribute:

**Request**:

```
GET /v1/boards?link=home HTTP/1.1
Accept: application/json
```

wih the same response format as above.

(FIXME: above not yet implemented)

### POST /v1/boards

**Request**:

```
POST /v1/boards HTTP/1.1
Content-Type: application/json
Accept: application/json
Content-Length: ...

{
  "name"  : "New Board",
  "notes" : "...",
  "link"  : "new",
  "code"  : "..."
}
```

**Response**:

```
HTTP/1.1 201 Created
Location: /v1/boards/42
```

### GET /v1/boards/:link

**Request**:

```
GET /v1/boards/home HTTP/1.1
Accept: application/json
```

**Response**:

```
HTTP/1.1 200 OK
Content-Type: application/json
Content-Length: ...

{
  "name"  : "Home",
  "notes" : "...",
  "href"  : "/boards/home",
  "code"  : "..."
}
```

### PUT /v1/boards/:id

**Request**:

```
PUT /v1/boards/42
Content-Type: application/json
Accept: application/json
Content-Length: ...

{
  "name": "New Name",
  "code": "..."
}
```

**Response**:

```
HTTP/1.1 204 No Content
```

### PATCH /v1/boards/:link

**Request**:

```
PATCH /v1/boards/home
Content-Type: application/json
Accept: application/json
Content-Length: ...

{
  "name": "New Name"
}
```

**Response**:

```
HTTP/1.1 204 No Content
```

### DELETE /v1/boards/home

**Request**:

```
DELETE /v1/boards/home HTTP/1.1
```

**Response**:

```
HTTP/1.1 204 No Content
```

## Queries

### GET /v1/query?q=...

Run a single query, and returns its results.

**Request**:

```
GET /v1/query?q=... HTTP/1.1
Accept: application/json
```

**Response**:

```
HTTP/1.1 200 OK
Content-Type: application/json
Content-Length: ...

{ ... }
```

### POST /v1/query

Run multiple queries, and returns the results of each, indexed in
such a way that the caller can tell one resultset from another.

**Request**:

```
POST /v1/query... HTTP/1.1
Content-Type: application/x-www-form-urlencoded
Accept: application/json
Content-Length: ...

{
  "cpu-usage": "SELECT cpu WHERE env = prod",
  "mem-usage": "SELECT mem WHERE env = prod"
}
```

**Response**:

```
HTTP/1.1 200 OK
Content-Type: application/json
Content-Length: ...

{
  "cpu-usage": {
    "cpu" : [
       {"t":1513785194,"v":1.2345},
       {"t":1513785195,"v":1.2301},
       {"t":1513785196,"v":1.2317},
       ...
    }
  },
  "mem-usage": {
    "mem" : [
      {"t":1513785194,"v":67.34},
      {"t":1513785195,"v":67.32},
      {"t":1513785196,"v":67.34},
       ...
    }
  }
}
```

