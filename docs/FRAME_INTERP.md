# Frame interpolation (stage A: analysis only)

What this is, what it is not, and how to read its log line.

## The idea

MH4U draws one picture per 30 fps logic frame. Zakuro (`zakuro-gpu/src/interpolation.rs`)
shows a 60 Hz picture from that by drawing a second, in-between picture for each logic frame:
each draw is matched with the same draw of the frame before, and when the vertex shader's float
uniforms that place the vertices (the matrices) moved smoothly, the draw is repeated with those
uniforms halfway. The game's own logic then runs at 30 and only the drawing runs at 60.

Matching the draws and deciding what moved is the part that needs no changes to how Azahar
renders. That is what `patches/0007` adds, ported from Zakuro:

* `video_core/frame_interp/frame_interp.{h,cpp}`: no Azahar dependency. The draw matcher
  (nearest draw drawn the same way, jump detection, restless draws, steps between round
  values) and the PICA vertex shader analysis (which float uniforms feed the position, normal,
  view and texture coordinate outputs, following temporaries, calls, ifs and loops).
* `video_core/frame_interp/frame_interp_driver.{h,cpp}`: reads each accelerated draw's state off
  the PICA registers (program, vertex arrays, buffers, first texture, output map) and feeds the
  matcher. Hooked into `RasterizerVulkan::Draw` and `TickFrame`.

## What it does not do

**Nothing is drawn from it.** There are no twin buffers, no second draw and no in-between
picture: stage B is not written, so this makes no frame faster. It only measures, so we can see
whether MH4U's draws match well enough to be worth building stage B for.

## Turning it on

Settings > Display > "Frame interpolation analysis (log only)", or `frame_interp_stats = on` in
`mh4u-app.ini`, or the environment variable `AZAHAR_FRAME_INTERP=1`. Takes effect on the next
game start. Off by default; when off a draw costs one flag check.

## Reading the log

Every 5 seconds, with Render at Info level:

    frame interpolation analysis (nothing is drawn from it): 30.0 game frames/s, 510 draws/frame
    (0.0 on the CPU, 0.0 geometry); 91% matched the frame before, 63% moved smoothly, 1.2%
    jumped (+0.4% keep jumping); 88% of frames smooth; analysis cost 3.1 us/draw

* **game frames/s**: frames in which the game drew something. 30 means the game runs at 30; the
  60 fps hook gives about 60.
* **on the CPU / geometry**: draws whose vertices are shaded on the CPU, or go through a
  geometry shader. Their matrices are not in the uniforms, so they are counted but never moved.
* **matched**: draws that had a draw of the same kind (same programs, vertex arrays, buffers and
  first texture) in the frame before. Low means the draws are not stable between frames, for
  example because the game streams its vertices to a different address each frame.
* **moved smoothly**: matched draws whose placing uniforms changed, and changed the way they did
  the frame before. This is what would be drawn again halfway.
* **jumped**: matched draws that changed in a way the frame before did not predict (a camera
  cut, a different model taken for this one, a flag). Drawn as they are. "Keep jumping" are
  those that jumped a few frames ago too, such as a sprite sheet that steps.
* **frames smooth**: frames where most draws matched and few jumped, which is where an
  in-between picture would look right.
* **analysis cost**: what the analysis takes per draw. If this is large, so would be stage B's
  bookkeeping.

What a good result looks like: matched above ~80%, jumped under ~5%, smooth above ~85% in a
fight, with the 60 fps hook off. A low match rate or many CPU-shaded draws means stage B would
not work well on this game, and we should know that before writing it.

## Stage B (not done)

For each draw that moved: draw it a second time, with the halfway uniforms, into twins of the
colour and depth buffers it draws into, and show the twin of the screen's buffer between two
frames. In Azahar this means twin images kept in step through clears, texture copies and
display transfers, the twin draws replayed after each render pass, and the in-between picture
presented before the real one. It touches the surface cache and the presentation path and
needs testing on a device; see `zakuro-gpu/src/raster/hardware.rs` for how Zakuro does it.

## Tests

    scripts/apply-patches.sh
    tools/frame-interp-test/run.sh

`test.cpp` has Zakuro's own nine interpolation tests ported with the same programs, numbers and
expectations, plus checks of the shader analysis on hand-assembled programs. `driver_test.cpp`
runs the driver against stand-ins for the PICA types.
