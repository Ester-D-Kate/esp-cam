# Convert an SD recording to an upright video

From the project directory, with FFmpeg and PowerShell installed:

```powershell
./tools/ffmpeg-sequence-to-video.ps1 -InputFolder "F:\recordings\2026-10-03_12-00-00" -Rotation clockwise
```

The current firmware saves **600 × 800 portrait** JPEGs. A quarter turn produces an **800 × 600 landscape** video. Choose `counterclockwise` if that direction suits the camera mounting, or `none` to keep portrait. `180` is also supported. Rotation is applied to the exported pixels; the ESP32 passes the sensor's JPEGs through to the raw MJPEG endpoint and SD without software rotation.

The converter uses `frames.csv` automatically. Gaps between recorded frames are preserved, so slow SD writes do not speed up the resulting video. The last frame lasts approximately 1/20 second. The first frame is the start of the exported video; any delay before the first captured frame is omitted. This is a variable frame rate MP4.

For old recordings without `frames.csv`, specify the frame rate that was actually used:

```powershell
./tools/ffmpeg-sequence-to-video.ps1 -InputFolder "F:\recordings\1" -FrameRate 15 -Rotation none
```

Use `-OutputFile` to choose the destination. Existing files are never overwritten. After an unexpected power loss, a truncated final CSV row may need removing before export; keep the original recording as a backup.
