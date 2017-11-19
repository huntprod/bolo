# Bolo HTTP REST API

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
    "uuid": "95b9c5f7-0469-40be-a45e-d982153e32d6",
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
  "95b9c5f7-0469-40be-a45e-d982153e32d6",
  "82ba004e-0cb3-4526-bffc-8eb203d674a5"
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
    "uuid"  : "ab68ab63-606c-4afd-ab5d-d0dbf11ea3af",
    "name"  : "Home",
    "link"  : "/boards/home",
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
GET /v1/boards?link=%2Fboards%2Fhome HTTP/1.1
Accept: application/json
```

wih the same response format as above.

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
  "link"  : "/boards/new",
  "code"  : "..."
}
```

**Response**:

```
HTTP/1.1 201 Created
Location: /v1/boards/44da6a1d-402e-44a8-a8ac-f8db3a3f9dd8
```

### GET /v1/boards/:uuid

**Request**:

```
GET /v1/boards/474ef93d-58bc-49fe-8663-daf3f528e051
Accept: application/json
```

**Response**:

```
HTTP/1.1 200 OK
Content-Type: application/json
Content-Length: ...

{
  "uuid"  : "474ef93d-58bc-49fe-8663-daf3f528e051",
  "name"  : "Home",
  "notes" : "...",
  "link"  : "/boards/home",
  "code"  : "..."
}
```

### PUT /v1/boards/:uuid

**Request**:

```
PUT /v1/boards/474ef93d-58bc-49fe-8663-daf3f528e051
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

### PATCH /v1/boards/:uuid

**Request**:

```
PATCH /v1/boards/474ef93d-58bc-49fe-8663-daf3f528e051
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

### DELETE /v1/boards/:uuid

**Request**:

```
DELETE /v1/boards/474ef93d-58bc-49fe-8663-daf3f528e051
```

**Response**:

```
HTTP/1.1 204 No Content
```

## Queries

### GET /v1/query?q=...

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

**Request**:

```
POST /v1/query... HTTP/1.1
Content-Type: application/x-www-form-urlencoded
Accept: application/json
Content-Length: ...

q=...
```

**Response**:

```
HTTP/1.1 200 OK
Content-Type: application/json
Content-Length: ...

{ ... }
```

