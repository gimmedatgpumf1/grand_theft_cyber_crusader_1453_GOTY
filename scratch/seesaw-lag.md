# The seesaw, measured — 2026-11-14 (second pass)

The King: *"It looks exceptionally bad still! It is not one smooth motion but
stop-move-stop-move!"* — and he was right, and the numbers said so: the plank's
angle did not move on four ticks in ten while its spin kept growing.

## The cause: the ball's walk was erasing the plank's turn

The bodies are stepped, then the ball walks. For every shape its walk meets it
carried the shape *back to the instant of the meeting*:

    met.angle = met.previousAngle + (met.angle - met.previousAngle) * when;

For a **pinned** body that expression is meaningless. Its "meeting" is found at
the face the ball is already lying on, a tenth of a picometre deep, so `when`
came out at 0.4 and the plank's turn was multiplied by it and thrown away —
while the spin that made the turn stayed in the plank. Over a swing this repeated
on almost every tick. That is what stop-move-stop-move was.

Measured, tick by tick (`scratch/seesaw-stutter.cpp`), before:

    t16..t24  step ~0, spin climbing -0.006 -> +0.073
    t21..t39  twenty ticks with no travel at all and a turn it never spent
    35 of 60 turning ticks lost part of their turn

## What was changed

1. **A refused piece of motion is motion the body does not have.**
   `PlaygroundRoom::absorbRefusedStep()` is now the one door for that, used by
   all four places that shorten a body's pose after the step was taken: the
   swept step, a pinned body's turn, the ball's arrival prevention, and a pinned
   body being turned back out of something. Refused travel *and* refused turn are
   both given up, and the couple is written at the pin where the bearing took it.

2. **A pinned body is not carried back by the ball's walk, nor refused by the
   arrival pre-check.** It has no place to be carried to — its place is its axle.
   Preventing the ball from going inside the plank is the ball's own walk, and
   that is measured: over a 200-tick swing with the plank free to turn under the
   ball, the deepest the ball ever was inside the plank was **1.1e-16 m** (the
   last bit of a double) and nothing was ever lifted out.

3. **A gap under a micrometre is touching, not overlap.** `separateBall()` was
   entering its repair path on -1e-16 m, and since a pinned body cannot be
   pushed out of anything, the ball was moved by hand and counted as a repair.
   The count now reads zero; the room's resolution is a micrometre and this is
   the same rule the rest of the room already used.

## After

    35 of 60 turning ticks lost part of their turn  ->  11 of 60

and all eleven are the **floor** stopping the plank at its corner limit
(-0.8852 rad), which is a seesaw hitting the ground, not a stutter. Between them
the swing is monotone to the millisecond:

    t11 +0.08167  t12 +0.02793  t13 -0.02559  t14 -0.08535  t15 -0.14748
    t16 -0.21240  t17 -0.27844  t18 -0.34540  t19 -0.41320  t20 -0.48165
    t21 -0.55057  t22 -0.61984  t23 -0.68938  t24 -0.75928  t25 -0.82960

The clip is `scratch/seesaw-swing.gif` (60 frames, one per tick, 20 Hz) and the
strip `scratch/seesaw-swing-strip.png`.

## The checks, all re-run after the change

    hitscan-check     0 of 684 turns disagreed (was 3, worst 0.0551)
    held-speed        0.0000 m/s held by a body that stood still
    king-exact        0.000 m/s, air drawn 0.00 N
    drag-clip         0 mm overlap, 0 mm under the floor, through plank and ball
    seesaw-carry      0 mm into the plank, 0 lifted out, 0 repairs
    energy-blowup     +28.444 J (unchanged; it is the probe's own teleport)
    tick cost         0.013 ms a tick with the seesaw swinging under a ball
