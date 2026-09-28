param(
  [Parameter(Mandatory = $true)]
  [string]$InputFolder,

  [string]$OutputFile = "",

  [int]$FrameRate = 30,

  [int]$Crf = 18,

  [string]$Preset = "medium"
)

$resolvedInput = (Resolve-Path -Path $InputFolder).Path
$firstFrame = Join-Path $resolvedInput "000001.jpg"

if (-not (Test-Path -Path $firstFrame)) {
  Write-Error "Expected the folder to contain numbered JPEGs like 000001.jpg, 000002.jpg, 000003.jpg."
  exit 1
}

if ([string]::IsNullOrWhiteSpace($OutputFile)) {
  $folderName = Split-Path -Path $resolvedInput -Leaf
  $parentDir = Split-Path -Path $resolvedInput -Parent
  $OutputFile = Join-Path -Path $parentDir -ChildPath ($folderName + ".mp4")
}

$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
if (-not $ffmpeg) {
  Write-Error "ffmpeg was not found in PATH. Install ffmpeg first, then rerun this script."
  exit 1
}

$pattern = Join-Path -Path $resolvedInput -ChildPath "%06d.jpg"

& $ffmpeg.Source `
  -y `
  -framerate $FrameRate `
  -i $pattern `
  -c:v libx264 `
  -pix_fmt yuv420p `
  -crf $Crf `
  -preset $Preset `
  $OutputFile

if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

Write-Host "Video written to: $OutputFile"
