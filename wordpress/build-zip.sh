#!/bin/sh
# Builds dub-echo.zip (installable via Plugins > Install > Upload) next to this script.
set -e
cd "$(dirname "$0")"
rm -f dub-echo.zip
zip -rq dub-echo.zip dub-echo -x '*.DS_Store'
echo "dub-echo.zip: $(du -h dub-echo.zip | cut -f1)"
