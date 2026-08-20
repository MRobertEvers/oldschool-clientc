#!/bin/sh
# Pinned OSRS Wiki source for the pickpocket table (slice D11).
set -e
cd "$(dirname "$0")"
if curl -sf -A 'osrs239-content port (contact: repo owner)' \
    "https://oldschool.runescape.wiki/w/Thieving?action=raw" -o Thieving.wiki; then
  echo "Thieving.wiki $(wc -c < Thieving.wiki) bytes"
else
  echo "Thieving.wiki MISSING"; rm -f Thieving.wiki
fi
