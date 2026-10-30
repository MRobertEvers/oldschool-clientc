#!/bin/sh
# A stand-in for a server binary the live link launches (js5_server, io_server):
# records its command line beside the mock peer's log, then idles until it is
# stopped. The editor server owns its lifetime; the test checks it ends.
echo "$0 $*" >> "$MOCK_PEER_LOG.js5"
exec sleep 600
