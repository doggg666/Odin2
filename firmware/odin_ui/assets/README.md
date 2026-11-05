# Home artwork

`odin-reference.jpg` is the artwork supplied by the user to replace the Home
waveform. Its source filename was
`odin_by_design_by_humans_d8uyqd7-375w-2x.jpg`, hosted on the Wix/DeviantArt
image CDN. The original 600x600 JPEG is retained unchanged.

`odin_face.rgb565` is its 340x340 display representation: opaque RGB565 pixels,
little-endian, without a file header. It is embedded in the application image
and displayed with LVGL's native image widget. It does not require internet
access or runtime JPEG decoding.

Regenerate it using FFmpeg from the firmware directory:

```bash
ffmpeg -v error -y -i odin_ui/assets/odin-reference.jpg \
  -vf 'scale=340:340:flags=lanczos' -pix_fmt rgb565le \
  -f rawvideo odin_ui/assets/odin_face.rgb565
```

The artwork remains attributed to its original creator; this project does not
claim authorship of the supplied image.
