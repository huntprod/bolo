FROM scratch
COPY root /

ENV BOLO_CORE=bolo:2001 SESSION_LIMIT=60
CMD ["/reexec", "BOLO_CORE", "SESSION_LIMIT", "--", "/api", "--db", "/db/web.db", "--root", "/htdocs", "--listen", ":80"]
EXPOSE 80
