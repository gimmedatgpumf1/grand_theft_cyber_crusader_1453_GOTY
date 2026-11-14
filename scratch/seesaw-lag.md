# The seesaw, measured — 2026-11-14

The King: *"The seesaw is laggy AF! It behaven in unnatural way! It is slow!"*

Three things were timed or counted, not guessed.

## 1. A tick costs a millisecond, and a frame costs ten

    600 ticks in 521.1 ms  (0.869 ms/tick, 1151 ticks/s)     scratch/timing.cpp
    60 frames at 1280x720 in soft rasteriser: 9.2 ms each     scratch/frame-cost.cpp

At 20 Hz the room has 50 ms a tick and uses 0.87. A frame drawn the slow way
(software rasteriser) costs 9.2 ms, so the loop hands the room its full share of
real time: `min(0.05, 1/fps) * fps = 1.000 s` of room time per second of real
time. Nothing here is running in slow motion.

## 2. The plank's dynamics are the plank's dynamics

Plank: 2.0 m x 0.24 m, four corners at 1 kg, I = 1.352 kg m^2 about the pin.
Ball 0.62 kg at 0.80 m. Predicted angular acceleration of a loaded plank is
about 2 rad/s^2 (2.4 with the ball pinned, less as it rolls). Measured over the
first tick of a loaded swing: 0.125 rad/s in 0.05 s = **2.5 rad/s^2**. The axle
is not the weight holding it back, and the bearing is 0.047 N m.

## 3. The defect that was there: a refused turn kept its spin

`pivotTurnAllowed()` answers a *fraction* of the turn the plank wanted, and the
caller was adding `turn * fraction` to the angle while leaving the spin whole.
The plank then had momentum it had not spent, the next tick's sweep was asked
about a turn beginning from a speed the plank did not have, and the sweep's own
answer disagreed with what the plank actually did:

    scratch/hitscan-check, before:  3 of 684 turns disagreed, worst 0.0551 rad
    scratch/hitscan-check, after:   0 of 684 turns disagreed, worst 0

Refused turn is now absorbed exactly as refused travel is for a free body, and
the couple that took it is written at the pin, where the bearing took it.

An earlier attempt also relaxed `pivotTurnAllowed()`'s pre-check from "deeper
than a picometre" to "deeper than a micrometre" (touching is not inside). It
changed no behaviour that could be measured and made the sweep check worse
(12 of 684), so it was reverted; only the absorption stands.

## 4. What the swing looks like

`scratch/seesaw-swing.cpp` -> `scratch/seesaw-swing-strip.png`. The ball is let
go on the raised end at 0.40 rad; the plank swings down, the ball rolls down it
and off, the plank is left turning the other way under the ball it has picked
up, and comes to rest against the floor at +0.8852 rad. No clip, nothing
floating, nothing left spinning in place.

## 5. What is still refused, and why

A pinned body's turn is still stopped at the first instant it would go *into*
the ball or another shape (`pivotTurnAllowed`). A plank turning up into a ball
resting on it is such a turn, and it is refused — the ball is on the far side of
the face and the plank's own step is swept before the ball's. That is the one
place where the plank can still be held back by something it is carrying.

    scratch/seesaw-lag.cpp: 36 of 46 turning ticks lost part of their turn (78%)
    in the loaded swing, most of it at the floor's corner limit, which is the
    floor and not a fault.
