#!/bin/sh

HASH="$(sha1sum -b "$1")"
HASH="${HASH%% *}"

mv "$1" "../files/$HASH.$2"

echo "Status: 200 OK"
echo
echo "{\"data\": {\"filePath\": \"files/$HASH.$2\"}}"
