param(
  [Parameter(Mandatory = $true)] [string]$InputFolder,
  [string]$OutputFile = "",
  [ValidateRange(1, 60)] [int]$FrameRate = 20,
  [ValidateSet("clockwise", "counterclockwise", "none", "180")] [string]$Rotation = "clockwise",
  [ValidateRange(0, 51)] [int]$Crf = 18,
  [string]$Preset = "medium"
)

$ErrorActionPreference = "Stop"
$resolvedInput = (Resolve-Path -LiteralPath $InputFolder).Path
if (-not (Test-Path -LiteralPath (Join-Path $resolvedInput "000001.jpg"))) {
  throw "Expected numbered JPEG frames beginning at 000001.jpg."
}
$ffmpeg = Get-Command ffmpeg -ErrorAction Stop
if ([string]::IsNullOrWhiteSpace($OutputFile)) {
  $OutputFile = Join-Path (Split-Path $resolvedInput -Parent) ((Split-Path $resolvedInput -Leaf) + ".mp4")
}
$OutputFile = [System.IO.Path]::GetFullPath($OutputFile)
if (Test-Path -LiteralPath $OutputFile) { throw "Output already exists: $OutputFile" }
$filter = switch ($Rotation) {
  "clockwise" { "transpose=1" }
  "counterclockwise" { "transpose=2" }
  "180" { "hflip,vflip" }
  "none" { "null" }
}
$indexPath = Join-Path $resolvedInput "frames.csv"
$concatPath = $null
$writer = $null
try {
  if (Test-Path -LiteralPath $indexPath) {
    # Keep timestamp gaps caused by dropped frames, instead of speeding up the video.
    $concatPath = Join-Path (Split-Path $OutputFile -Parent) (([guid]::NewGuid().ToString()) + ".ffconcat")
    $writer = [System.IO.StreamWriter]::new($concatPath, $false, [System.Text.UTF8Encoding]::new($false))
    $writer.WriteLine("ffconcat version 1.0")
    $previous = $null
    $lastPath = $null
    Import-Csv -LiteralPath $indexPath | ForEach-Object {
      $row = $_
      if ($row.file -notmatch '^\d{6,10}\.jpg$' -or $row.elapsed_ms -notmatch '^\d+$') {
        throw "Invalid frame index row. Recover/remove the incomplete final row if power was lost."
      }
      $path = Join-Path $resolvedInput $row.file
      if (-not (Test-Path -LiteralPath $path)) { throw "Missing recorded frame: $path" }
      if ($null -ne $previous) {
        $delta = [long]$row.elapsed_ms - [long]$previous.elapsed_ms
        if ($delta -le 0) { throw "Frame timestamps must increase." }
        $writer.WriteLine("duration " + ($delta / 1000.0).ToString("0.000000", [cultureinfo]::InvariantCulture))
      }
      $lastPath = $path.Replace('\', '/').Replace("'", "'\''")
      $writer.WriteLine("file '$lastPath'")
      $writer.WriteLine("option framerate 1000")
      $previous = $row
    }
    if ($null -eq $previous) { throw "No completed frames in frames.csv." }
    $writer.WriteLine("duration " + (1.0 / $FrameRate).ToString("0.000000", [cultureinfo]::InvariantCulture))
    $writer.WriteLine("file '$lastPath'")
    $writer.WriteLine("option framerate 1000")
    $writer.Dispose()
    $writer = $null
    $inputArgs = @('-f', 'concat', '-safe', '0', '-i', $concatPath)
  } else {
    # Legacy recordings have no timestamps; supply their original frame rate.
    $inputArgs = @('-framerate', "$FrameRate", '-i', (Join-Path $resolvedInput '%06d.jpg'))
  }
  & $ffmpeg.Source -n @inputArgs -vf $filter -fps_mode vfr -c:v libx264 -pix_fmt yuv420p -crf $Crf -preset $Preset -video_track_timescale 1000 $OutputFile
  if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed with exit code $LASTEXITCODE" }
  Write-Host "Video written to: $OutputFile"
} finally {
  if ($null -ne $writer) { $writer.Dispose() }
  if ($null -ne $concatPath -and (Test-Path -LiteralPath $concatPath)) {
    Remove-Item -LiteralPath $concatPath
  }
}
