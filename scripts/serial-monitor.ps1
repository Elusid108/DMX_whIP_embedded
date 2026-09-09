# Refresh the ESP32 COM3 serial viewer window.
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$python = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\python.exe"
$script = Join-Path $PSScriptRoot "serial-monitor.py"
$log = Join-Path $repo ".pio\serial-com3.log"
$title = "ESP32 COM3"

Get-Process | Where-Object { $_.MainWindowTitle -eq $title } | ForEach-Object {
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}
Get-CimInstance Win32_Process | Where-Object {
    $_.CommandLine -and $_.CommandLine -match 'serial-monitor.py'
} | ForEach-Object {
    Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 400

$cmd = @"
`$Host.UI.RawUI.WindowTitle = '$title'
& '$python' '$script' --port COM3 --baud 115200 --log '$log'
"@
Start-Process -FilePath "powershell.exe" -ArgumentList @("-NoExit", "-Command", $cmd)
Write-Host "Launched $title"
