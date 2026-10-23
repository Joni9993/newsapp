param([switch]$Flash, [string]$Port = 'COM12')
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
# export.ps1 schreibt Hinweise auf stderr; mit 'Stop' bricht PowerShell 5.1 daran ab
. "$env:USERPROFILE\esp\esp-idf\export.ps1" 2>$null | Out-Null
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
# Ein Syntaxfehler in app.js legt die ganze Webseite lahm - vor dem Bauen pruefen
if (Get-Command node -ErrorAction SilentlyContinue) {
    & node --check main/app.js
    if ($LASTEXITCODE -ne 0) { Write-Error 'app.js hat einen Syntaxfehler' }
}
& python "$env:IDF_PATH\tools\idf.py" build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Flash) {
    & python -m serial.tools.list_ports -v
    & python "$env:IDF_PATH\tools\idf.py" -p $Port flash
    exit $LASTEXITCODE
}
