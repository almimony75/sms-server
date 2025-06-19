#!/bin/bash

# s.sh — Simple SSE client using curl
curl -N -s http://localhost:8081/events | \
while read -r line; do
  if [[ "$line" == data:* ]]; then
    json="${line#data: }"
    echo -e "\n📩 New SMS Received:"
    echo "$json" | jq . 2>/dev/null || echo "$json"
  fi
done
