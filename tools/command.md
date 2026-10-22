# Convert an SD recording to an upright video

From the project directory, with FFmpeg and PowerShell installed:

```powershell
./tools/ffmpeg-sequence-to-video.ps1 -InputFolder "F:\recordings\2026-10-03_12-00-00" -Rotation clockwise
```

The firmware saves **OV3660: 600 × 800 portrait** or **OV2640: 800 × 600 landscape** JPEGs, with other valid aligned native sizes accepted in the sensor's required orientation. A quarter turn swaps the actual dimensions. Choose `counterclockwise` if that direction suits the camera mounting, or `none` to preserve the sensor's native orientation. `180` is also supported. Rotation is applied to the exported pixels; the ESP32 passes JPEGs to the raw MJPEG endpoint and SD without software rotation.

For OV2640 recordings in normal landscape, pass `-Rotation none` explicitly because the script defaults to clockwise rotation:

```powershell
./tools/ffmpeg-sequence-to-video.ps1 -InputFolder "F:\recordings\2026-10-03_12-00-00" -Rotation none
```

The converter uses `frames.csv` automatically. Gaps between recorded frames are preserved, so slow SD writes do not speed up the resulting video. The last frame lasts approximately 1/20 second. The first frame is the start of the exported video; any delay before the first captured frame is omitted. This is a variable frame rate MP4.

For old recordings without `frames.csv`, specify the frame rate that was actually used:

```powershell
./tools/ffmpeg-sequence-to-video.ps1 -InputFolder "F:\recordings\1" -FrameRate 15 -Rotation none
```

Use `-OutputFile` to choose the destination. Existing files are never overwritten. After an unexpected power loss, a truncated final CSV row may need removing before export; keep the original recording as a backup.
