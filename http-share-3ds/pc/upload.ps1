# HTTP Share 3DS - upload a file from Windows (PowerShell)
# Usage:
#   .\upload.ps1 -Ip 192.168.5.119 -File "C:\path\to\game.cia"
#   .\upload.ps1 -Ip 192.168.5.119 -File "C:\path\to\game.cia" -SubDir MyGames
param(
    [Parameter(Mandatory=$true)][string]$Ip,
    [Parameter(Mandatory=$true)][string]$File,
    [string]$SubDir = "",
    [int]$Port = 8080
)

if (-not (Test-Path $File)) {
    Write-Error "File not found: $File"
    exit 1
}

$boundary = "----HTTPShare3DS" + [guid]::NewGuid().ToString("N")
$fileName = [System.IO.Path]::GetFileName($File)
$lf = "`r`n"

# Build multipart body as bytes (binary safe)
$head = "--$boundary$lf" +
        "Content-Disposition: form-data; name=`"path`"$lf$lf" +
        "$SubDir$lf" +
        "--$boundary$lf" +
        "Content-Disposition: form-data; name=`"file`"; filename=`"$fileName`"$lf" +
        "Content-Type: application/octet-stream$lf$lf"
$tail = "$lf--$boundary--$lf"

$headBytes = [System.Text.Encoding]::UTF8.GetBytes($head)
$fileBytes = [System.IO.File]::ReadAllBytes($File)
$tailBytes = [System.Text.Encoding]::UTF8.GetBytes($tail)

$body = New-Object byte[] ($headBytes.Length + $fileBytes.Length + $tailBytes.Length)
[Array]::Copy($headBytes, 0, $body, 0, $headBytes.Length)
[Array]::Copy($fileBytes, 0, $body, $headBytes.Length, $fileBytes.Length)
[Array]::Copy($tailBytes, 0, $body, $headBytes.Length + $fileBytes.Length, $tailBytes.Length)

$url = "http://${Ip}:${Port}/upload"
Write-Host "Uploading $fileName ($([math]::Round($body.Length/1KB,1)) KB) -> $url ..."

try {
    $resp = Invoke-WebRequest -Uri $url -Method Post -Body $body `
        -ContentType "multipart/form-data; boundary=$boundary" `
        -UseBasicParsing -TimeoutSec 600
    Write-Host "OK: $($resp.StatusCode) $($resp.Content)"
} catch {
    Write-Error "Upload failed: $($_.Exception.Message)"
    exit 1
}
