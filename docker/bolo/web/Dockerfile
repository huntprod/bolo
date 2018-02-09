FROM scratch
COPY root /

CMD ["/reexec", "--", "/api", "--db", "/db/web.db", "--root", "/htdocs", "--listen", ":80"]
EXPOSE 80
