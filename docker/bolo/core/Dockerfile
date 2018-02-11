FROM scratch
COPY root /

CMD ["/reexec", "--", "/bolo", "core", "--config", "/etc/bolo.conf", "-D"]
# TCP/2001 is the api back-channel
# TCP/2002 is the metric submission channel
EXPOSE 2001 2002
