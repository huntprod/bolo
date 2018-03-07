package main

import (
	"net/http"
	"os"
	"strings"

	fmt "github.com/jhunt/go-ansi"
)

const (
	CookieName = "bolo1"
)

type API struct {
	SessionLimit int
	Bolo         *Bolo
	DB           *DB
}

func (api *API) Session(w http.ResponseWriter, req *http.Request) *Session {
	c, _ := req.Cookie(CookieName)
	if c == nil || c.Value == "" {
		authRequired(w, "authorization required")
		return nil
	}

	sid := c.Value
	s, err := api.DB.GetSession(sid)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to retrieve session '%s' from database: %s\n", sid, err)
		respond(w, 500, "an unknown server error has occurred")
		return nil
	}

	if s == nil {
		authRequired(w, "authorization required")
		return nil
	}

	w.Header().Set("Set-Cookie", fmt.Sprintf("%s=%s; Path=/; HttpOnly; Max-Age=%d", CookieName, sid, api.SessionLimit))
	return s
}

func (api *API) ServeHTTP(w http.ResponseWriter, req *http.Request) {
	if req.URL.Path == "/v1/auth" {
		/* POST /v1/auth {{{ */
		if req.Method == "POST" {
			var in struct {
				Username string `json:"username"`
				Password string `json:"password"`
			}
			err := readJSON(&in, req)
			if err != nil {
				fmt.Fprintf(os.Stderr, "POST /v1/auth: %s\n", err)
				badRequest(w, "malformed JSON payload")
				return
			}
			if in.Username == "" {
				fmt.Fprintf(os.Stderr, "POST /v1/auth: missing username\n")
				badRequest(w, "missing username")
				return
			}
			if in.Password == "" {
				fmt.Fprintf(os.Stderr, "POST /v1/auth: missing password\n")
				badRequest(w, "missing password")
				return
			}

			u, err := api.DB.GetUser(0, in.Username)
			if err != nil {
				fmt.Fprintf(os.Stderr, "failed to find user '%s': %s\n", in.Username, err)
				accessDenied(w, "invalid username or password")
				return
			}
			if u == nil {
				fmt.Fprintf(os.Stderr, "failed to find user '%s'\n", in.Username)
				accessDenied(w, "invalid username or password")
				return
			}

			if !u.Authenticate(in.Password) {
				accessDenied(w, "invalid username or password")
				return
			}

			sid := randomString(64)
			if sid == "" {
				fmt.Fprintf(os.Stderr, "failed to generate session id -- is the crypto library acting up?\n")
				respond(w, 500, "an unknown server error has occurred")
				return
			}

			err = api.DB.Insert(&Session{
				SID:  sid,
				User: u,
			})
			if err != nil {
				fmt.Fprintf(os.Stderr, "failed to create session in database: %s\n", err)
				respond(w, 500, "an unknown server error has occurred")
				return
			}

			w.Header().Set("Set-Cookie", fmt.Sprintf("%s=%s; Path=/; HttpOnly; Max-Age=%d", CookieName, sid, api.SessionLimit))
			w.WriteHeader(204)
			return
		}
		/* }}} */
		/* GET /v1/auth {{{ */
		if req.Method == "GET" {
			s := api.Session(w, req)
			if s == nil {
				return
			}

			respondWith(w, 200, struct {
				Username    string `json:"username"`
				DisplayName string `json:"display_name"`
				Access      string `json:"access"`
			}{
				Username:    s.User.Username,
				DisplayName: s.User.Name,
				Access:      s.User.Access,
			})
			return
		}
		/* }}} */
		/* DELETE /v1/auth {{{ */
		if req.Method == "DELETE" {
			c, _ := req.Cookie(CookieName)
			if c == nil {
				w.WriteHeader(204)
				return
			}
			sid := c.Value
			s, err := api.DB.GetSession(sid)
			if err != nil {
				fmt.Fprintf(os.Stderr, "failed to retrieve session '%s' from database: %s\n", sid, err)
				respond(w, 500, "an unknown server error has occurred")
				return
			}

			if s != nil {
				err = api.DB.Delete(s)
				if err != nil {
					fmt.Fprintf(os.Stderr, "failed to remove session '%s' from database: %s\n", sid, err)
					respond(w, 500, "an unknown server error has occurred")
					return
				}
			}

			w.Header().Set("Set-Cookie", fmt.Sprintf("%s=%s; Expires=Thu, 01 Jan 1970 00:00:00 GMT", CookieName, sid))
			w.WriteHeader(204)
			return
		}
		/* }}} */

		w.WriteHeader(405)
		fmt.Fprintf(w, "method %s not allowd.\n", req.Method)
		return
	}

	if req.URL.Path == "/v1/nav" {
		/* GET /v1/nav {{{ */
		if req.Method == "GET" {
			s := api.Session(w, req)
			if s == nil {
				return
			}

			boards, err := api.DB.GetBoardsFor(s.User)
			if err != nil {
				fmt.Fprintf(os.Stderr, "GET /v1/nav: %s\n", err)
				respond(w, 500, "an unknown server error has occurred")
				return
			}

			type Result struct {
				Name string `json:"name"`
				Link string `json:"link"`
				Code string `json:"code"`
			}
			ll := make([]Result, len(boards))
			for i := range boards {
				ll[i].Name = boards[i].Name
				ll[i].Link = boards[i].Link
				ll[i].Code = boards[i].Code
			}

			respondWith(w, 200, ll)
			return
		}
		/* }}} */
		/* PUT /v1/nav {{{ */
		if req.Method == "PUT" {
			s := api.Session(w, req)
			if s == nil {
				return
			}

			var in []string
			err := readJSON(&in, req)
			if err != nil {
				fmt.Fprintf(os.Stderr, "PUT /v1/nav: %s\n", err)
				badRequest(w, "malformed JSON payload")
				return
			}

			err = api.DB.SetNavs(s.User, in)
			if err != nil {
				fmt.Fprintf(os.Stderr, "PUT /v1/nav: %s\n", err)
				respond(w, 500, "an unknown server error has occurred")
				return
			}

			w.WriteHeader(204)
			return
		}
		/* }}} */

		w.WriteHeader(405)
		fmt.Fprintf(w, "method %s not allowd.\n", req.Method)
		return
	}

	if req.URL.Path == "/v1/boards" {
		/* GET /v1/boards {{{ */
		if req.Method == "GET" {
			if s := api.Session(w, req); s == nil {
				return
			}

			bb, err := api.DB.GetBoards()
			if err != nil {
				fmt.Fprintf(os.Stderr, "failed to retrieve list of boards from database: %s\n", err)
				respond(w, 500, "an unknown server error has occurred")
				return
			}

			type Result struct {
				Name  string `json:"name"`
				Link  string `json:"link"`
				Notes string `json:"notes"`
				Code  string `json:"code"`
			}
			lst := make([]Result, len(bb))
			for i := range bb {
				lst[i].Name = bb[i].Name
				lst[i].Link = bb[i].Link
				lst[i].Notes = bb[i].Notes
				lst[i].Code = bb[i].Code
			}

			respondWith(w, 200, lst)
			return
		}
		/* }}} */
		/* POST /v1/boards {{{ */
		if req.Method == "POST" {
			s := api.Session(w, req)
			if s == nil {
				return
			}

			var in struct {
				Name  string `json:"name"`
				Link  string `json:"link"`
				Notes string `json:"notes"`
				Code  string `json:"code"`
			}
			err := readJSON(&in, req)
			if err != nil {
				fmt.Fprintf(os.Stderr, "POST /v1/boards: %s\n", err)
				badRequest(w, "malformed JSON payload")
				return
			}
			fmt.Fprintf(os.Stderr, "[[%v]]\n", in)
			if in.Name == "" {
				fmt.Fprintf(os.Stderr, "POST /v1/boards: missing name\n")
				badRequest(w, "missing name")
				return
			}
			if len(in.Name) > 100 {
				fmt.Fprintf(os.Stderr, "POST /v1/boards: name too long (>100 characters)\n")
				badRequest(w, "name too long (>100 characters)")
				return
			}
			if in.Link == "" {
				fmt.Fprintf(os.Stderr, "POST /v1/boards: missing link\n")
				badRequest(w, "missing link")
				return
			}
			if len(in.Link) > 100 {
				fmt.Fprintf(os.Stderr, "POST /v1/boards: link too long (>100 characters)\n")
				badRequest(w, "link too long (>100 characters)")
				return
			}
			if in.Code == "" {
				fmt.Fprintf(os.Stderr, "POST /v1/boards: missing code\n")
				badRequest(w, "missing code")
				return
			}

			board := &Board{
				Name:  in.Name,
				Link:  in.Link,
				Notes: in.Notes,
				Code:  in.Code,
				Owner: s.User,
			}
			if err := api.DB.Insert(board); err != nil {
				fmt.Fprintf(os.Stderr, "POST /v1/boards: %s\n", err)
				respond(w, 500, "unable to create new board")
				return
			}

			respondWith(w, 200, struct {
				Name  string `json:"name"`
				Link  string `json:"link"`
				Notes string `json:"notes"`
				Code  string `json:"code"`
			}{
				Name:  board.Name,
				Link:  board.Link,
				Notes: board.Notes,
				Code:  board.Code,
			})
			return
		}
		/* }}} */

		w.WriteHeader(405)
		fmt.Fprintf(w, "method %s not allowd.\n", req.Method)
		return
	}

	if strings.HasPrefix(req.URL.Path, "/v1/boards/") {
		id := strings.TrimPrefix(req.URL.Path, "/v1/boards/")

		/* GET /v1/boards/:id {{{ */
		if req.Method == "GET" {
			if s := api.Session(w, req); s == nil {
				return
			}

			b, err := api.DB.GetBoard(0, id)
			if err != nil {
				respond(w, 500, "unable to retrieve board")
				return
			}
			if b == nil {
				respond(w, 404, "board not found")
				return
			}

			respondWith(w, 200, struct {
				Name  string `json:"name"`
				Link  string `json:"link"`
				Notes string `json:"notes"`
				Code  string `json:"code"`
			}{
				Name:  b.Name,
				Link:  b.Link,
				Notes: b.Notes,
				Code:  b.Code,
			})
			return
		}
		/* }}} */
		/* PUT /v1/boards/:id {{{ */
		if req.Method == "PUT" {
			s := api.Session(w, req)
			if s == nil {
				return
			}

			b, err := api.DB.GetBoard(0, id)
			if err != nil {
				respond(w, 500, "unable to retrieve board")
				return
			}
			if b == nil || b.Owner.ID != s.User.ID {
				respond(w, 404, "board not found")
				return
			}

			var in struct {
				Name  string `json:"name"`
				Notes string `json:"notes"`
				Code  string `json:"code"`
			}
			err = readJSON(&in, req)
			if err != nil {
				fmt.Fprintf(os.Stderr, "PUT /v1/boards/%s: %s\n", id, err)
				badRequest(w, "malformed JSON payload")
				return
			}
			if in.Name == "" {
				fmt.Fprintf(os.Stderr, "PUT /v1/boards/%s: missing name\n", id)
				badRequest(w, "missing name")
				return
			}
			if len(in.Name) > 100 {
				fmt.Fprintf(os.Stderr, "POST /v1/boards: name too long (>100 characters)\n")
				badRequest(w, "name too long (>100 characters)")
				return
			}
			if in.Code == "" {
				fmt.Fprintf(os.Stderr, "PUT /v1/boards/%s: missing code\n", id)
				badRequest(w, "missing code")
				return
			}

			b.Name = in.Name
			b.Notes = in.Notes
			b.Code = in.Code

			err = api.DB.Update(b)
			if err != nil {
				respond(w, 500, "unable to update board")
				return
			}

			respondWith(w, 200, struct {
				Name  string `json:"name"`
				Link  string `json:"link"`
				Notes string `json:"notes"`
				Code  string `json:"code"`
			}{
				Name:  b.Name,
				Link:  b.Link,
				Notes: b.Notes,
				Code:  b.Code,
			})
			return
		}
		/* }}} */
		/* PATCH /v1/boards/:id {{{ */
		if req.Method == "PATCH" {
			s := api.Session(w, req)
			if s == nil {
				return
			}

			b, err := api.DB.GetBoard(0, id)
			if err != nil {
				respond(w, 500, "unable to retrieve board")
				return
			}
			if b == nil || b.Owner.ID != s.User.ID {
				respond(w, 404, "board not found")
				return
			}

			var in struct {
				Name  string `json:"name"`
				Notes string `json:"notes"`
				Code  string `json:"code"`
			}
			err = readJSON(&in, req)
			if err != nil {
				fmt.Fprintf(os.Stderr, "PATCH /v1/boards/%s: %s\n", id, err)
				badRequest(w, "malformed JSON payload")
				return
			}
			if len(in.Name) > 100 {
				fmt.Fprintf(os.Stderr, "PATCH /v1/boards: name too long (>100 characters)\n")
				badRequest(w, "name too long (>100 characters)")
				return
			}

			if in.Name != "" {
				b.Name = in.Name
			}
			if in.Notes != "" {
				b.Notes = in.Notes
			}
			if in.Code != "" {
				b.Code = in.Code
			}

			err = api.DB.Update(b)
			if err != nil {
				respond(w, 500, "unable to update board")
				return
			}

			respondWith(w, 200, struct {
				Name  string `json:"name"`
				Link  string `json:"link"`
				Notes string `json:"notes"`
				Code  string `json:"code"`
			}{
				Name:  b.Name,
				Link:  b.Link,
				Notes: b.Notes,
				Code:  b.Code,
			})
			return
		}
		/* }}} */
		/* DELETE /v1/boards/:id {{{ */
		if req.Method == "DELETE" {
			s := api.Session(w, req)
			if s == nil {
				return
			}

			b, err := api.DB.GetBoard(0, id)
			if err != nil {
				respond(w, 500, "unable to retrieve board")
				return
			}
			if b == nil || b.Owner.ID != s.User.ID {
				respond(w, 404, "board not found")
				return
			}

			err = api.DB.Delete(b)
			if err != nil {
				respond(w, 500, "unable to delete board")
				return
			}

			w.WriteHeader(204)
			return
		}
		/* }}} */

		w.WriteHeader(405)
		fmt.Fprintf(w, "method %s not allowd.\n", req.Method)
		return
	}

	if strings.HasPrefix(req.URL.Path, "/v1/lib/") {
		id := strings.TrimPrefix(req.URL.Path, "/v1/lib/")

		/* GET /v1/lib/:id {{{ */
		if req.Method == "GET" {
			if s := api.Session(w, req); s == nil {
				return
			}

			var resp struct {
				Code string `json:"code"`
			}

			if code, ok := StandardLibrary[id]; ok {
				resp.Code = code

			} else {
				b, err := api.DB.GetBoard(0, id)
				if err != nil {
					respond(w, 500, "unable to retrieve board")
					return
				}
				if b == nil {
					respond(w, 404, "lib not found")
					return
				}
				resp.Code = b.Code
			}

			respondWith(w, 200, resp)
			return
		}
		/* }}} */

		w.WriteHeader(405)
		fmt.Fprintf(w, "method %s not allowd.\n", req.Method)
		return
	}

	if req.URL.Path == "/v1/plan" {
		/* POST /v1/plan {{{ */
		if req.Method == "POST" {
			s := api.Session(w, req)
			if s == nil {
				return
			}

			var in struct {
				Q string `json:"q"`
			}
			err := readJSON(&in, req)
			if err != nil {
				fmt.Fprintf(os.Stderr, "POST /v1/plan: %s\n", err)
				badRequest(w, "malformed JSON payload")
				return
			}
			if in.Q == "" {
				fmt.Fprintf(os.Stderr, "POST /v1/plan: missing query parameter (q)\n")
				badRequest(w, "missing query parameter (q)")
				return
			}
			fields, err := api.Bolo.Plan(in.Q)
			if err != nil {
				respond(w, 500, err.Error())
				return
			}
			respondWith(w, 200, struct {
				Select []string `json:"select"`
			}{
				Select: fields,
			})
			return
		}
		/* }}} */

		w.WriteHeader(405)
		fmt.Fprintf(w, "method %s not allowd.\n", req.Method)
		return
	}

	if req.URL.Path == "/v1/query" {
		var q string

		/* GET /v1/query {{{ */
		if req.Method == "GET" {
			s := api.Session(w, req)
			if s == nil {
				return
			}

			if q = req.URL.Query().Get("q"); q == "" {
				fmt.Fprintf(os.Stderr, "GET /v1/query: missing query parameter (q)\n")
				badRequest(w, "missing query parameter (q)")
				return
			}
			api.Query(w, s.SID, q)
		}
		/* }}} */
		/* POST /v1/query {{{ */
		if req.Method == "POST" {
			s := api.Session(w, req)
			if s == nil {
				return
			}

			var in map[string]string
			err := readJSON(&in, req)
			if err != nil {
				fmt.Fprintf(os.Stderr, "POST /v1/query: %s\n", err)
				badRequest(w, "malformed JSON payload")
				return
			}
			results := make(map[string]map[string]Series)
			for id, q := range in {
				data, err := api.Bolo.Query(q)
				if err != nil {
					respond(w, 500, err.Error())
					return
				}
				fmt.Fprintf(os.Stderr, "POST /v1/query: results for {%s} (%s)\n", id, q)
				results[id] = data
			}
			fmt.Fprintf(os.Stderr, "respondiong with %d results\n", len(results))
			respondWith(w, 200, results)
			return
		}
		/* }}} */

		w.WriteHeader(405)
		fmt.Fprintf(w, "method %s not allowd.\n", req.Method)
		return
	}

	w.WriteHeader(404)
	fmt.Fprintf(w, "not found, so sad.\n")
}

func (api *API) Query(w http.ResponseWriter, sid string, q string) {
}
