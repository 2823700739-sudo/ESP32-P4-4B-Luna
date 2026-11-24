param([ValidateRange(1024,65535)][int]$Port = 8773)
$ErrorActionPreference = 'Stop'
$previewRepo = Split-Path -Parent $PSScriptRoot
$previewDocs = Join-Path $previewRepo 'docs'
$previewPython = Join-Path $previewRepo 'pc-agent/.venv/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $previewPython)) {
    $previewPython = (Get-Command python -ErrorAction Stop).Source
}
Write-Host "Luna UI preview: http://127.0.0.1:$Port/design/preview/"
Write-Host 'Demo data only. No COM ports, BLE, or media actions. Ctrl+C to stop.'
# Windows MIME registry may label SVG as image/svg, which Chromium will not draw.
$previewServer = @'
import http.server
import os
import sys

os.chdir(sys.argv[2])
http.server.SimpleHTTPRequestHandler.extensions_map['.svg'] = 'image/svg+xml'
http.server.test(HandlerClass=http.server.SimpleHTTPRequestHandler,
                 port=int(sys.argv[1]), bind='127.0.0.1')
'@
& $previewPython -c $previewServer $Port $previewDocs
exit $LASTEXITCODE
