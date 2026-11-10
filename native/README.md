# Native C++ runtime and portrait fitter

The native path is C++20 and offline-only. It does not start a browser, execute JavaScript, use Chromium, or download runtime assets.

- `gtc-native-client` connects to the authoritative server and renders its replicated world in a resizable landscape window that opens at **640×480**.
- `gtc-native-smoke` runs two real networked citizens through replication, customization, movement, pathfinding, and toilet-action checks.
- `gtc-native-fit` is the portrait fitting and review tool; it does not call a landmark detector during optimization.

## Build

Linux (ccache is selected through the repository's content-hash configuration):

```sh
./native/build.sh
```

Windows with Visual Studio 2022 or newer, and the **Desktop development with C++** workload:

```cmd
native\build.cmd
```

`native/build.ps1` is the PowerShell wrapper. The command initializes the Visual Studio environment when available and uses `/std:c++20`; the compiler reports unsupported options and errors directly. Linux executables are retained under `prebuilt/linux-x86_64/` with `prebuilt/NATIVE_SHA256SUMS`.

## Native multiplayer client and smoke test

For the automatic launcher, double-click **`run.cmd`** on Windows or run **`./run.sh`** on Linux.
Both build the server and native client, wait for the authoritative port, and open the native GUI.

To start the pieces manually from the repository root:

```sh
./build/bin/gtc1453 --port 1453 --web web
./build/bin/gtc-native-client --name "Native Citizen"
```

The client uses the existing `/ws` welcome/world, state, input, path, zip, attack, toilet, customize, and cheat messages. Path messages carry sprint/advanced flags and input carries block state. It sends no positions and has no offline player-only action path. Toilet requests never auto-walk.

The default is a resizable native Win32 window on Windows or a dynamically loaded **system X11 window** on Linux. Both platforms use the same fixed-function OpenGL scene renderer: WGL on Windows and dynamically loaded GLX/OpenGL on Linux. It provides textured triangle batches, cached texture uploads, GPU depth testing, direct scene presentation, and a CPU-composited native HUD uploaded as an alpha-blended overlay texture. Vertex buffer objects are used when available, with client-array fallback. The world-space scene and its VBO contents are retained across camera frames and rebuilt only for relevant simulation/view changes; camera movement and resizing only update matrices. Three rotating textures present the CPU-composited native HUD without normal-frame scene readback. On OpenGL 2.0-capable drivers, a GLSL 1.10 material path adds tangent-space skin normal maps, two-sided diffuse/specular lighting, rim response, fog, ACES-style tone mapping, and sRGB output; unsupported or failed shader setups retain the fixed-function path. Full-frame readback happens only for an explicit F12 screenshot. The native window title reports the actual OpenGL vendor/renderer and whether GLSL or fixed-function materials are active. The interactive loop has no sleep-based frame cap and requests uncapped swaps through standard WGL/GLX extensions when available. Authoritative citizen snapshots are interpolated at 15 Hz response. Rendering, ground picking, and toilet highlighting derive the same direct camera from the current mode and local body. First person is positioned at the local citizen’s morphed, posed head/eye joints while standing, walking, and seated, with no fixed eye height or forward offset; full body geometry remains rendered. Per-character GPU model matrices move the retained meshes with that interpolated state, while gameplay remains server-authoritative. Set `GTC_GL_DEBUG=1` to enable per-frame OpenGL error checks (otherwise those synchronization-prone checks stay out of the hot path). The HUD reports `GPU` only when that context is active. If GLX/OpenGL is unavailable, Linux retains its CPU raster/`XPutImage` fallback; Windows retains its GDI fallback. Linux has no X11 or OpenGL link-time/development-package dependency and no downloaded windowing runtime. Headless captures use the CPU renderer and do not validate a GPU context. On a machine without a display, use `--headless`; useful headless options include `--frames N`, `--benchmark N`, `--width`, `--height`, `--mode tps|fps|god`, `--path X Z`, `--move X Z`, `--toilet sit|shit|piss`, `--yaw`, `--pitch`, `--cheats` (draw the need panel into the capture), `--overlay toilet|manual` (draw that overlay into the capture), `--prompt NAME`, `--highlight` and `--hover N` (draw the interact prompt, light the toilet up, and mark the Nth clickable entry) and `--output PNG`.
A **Vulkan** backend now sits beside that OpenGL path, in `native/vulkan/`, behind the same seam: the rasterizer fills a `GpuScene`, a backend draws it, a `NativeFrame` comes back, and the window code does not care which one ran. It draws into an offscreen image with one render pass, one pipeline and one 96-byte push constant block (model matrix, colour, flags), then copies the image back into the frame the X11 path already knows how to present. It is tried first on both platforms and there is no switch that quietly puts the session on the other backend; the CPU rasterizer still backs the software path when no driver answers. Every entry point is resolved at run time from `libvulkan.so.1` through `vkGetInstanceProcAddr`, so the build has no link-time dependency on a loader. The shaders are ordinary GLSL in `native/vulkan/shaders/` (`skeleton.vert`, `skeleton.frag`), compiled with the Khronos reference compiler (`glslangValidator -V --target-env vulkan1.0`) and checked with `spirv-val`. The compiled modules and the C++ header generated from them are committed, so building the client needs no shader compiler; `python3 native/vulkan/make_shader_header.py --check` (run by `tests/run_tests.sh`, together with `spirv-val` when it is installed) fails if the header has drifted from the modules. This replaced a hand-emitted SPIR-V generator: one wrong opcode number in it produced a module that crashed Intel's shader compiler instead of being rejected. `gtc-native-smoke --vulkan-report` prints whether this machine can start the backend at all.

### Measuring a frame

Three benchmarks, each answering a different question:

    # One skull, every backend. Same triangles, same size, one after another.
    gtc-native-client --skull-benchmark 30 --renderer all --width 1568 --height 929 --root .

    # The street: the real world renderer with characters that never stop moving,
    # so nothing is a cache hit. `scene` draws no pixels and reports the CPU half.
    gtc-native-client --world-benchmark 60 --characters 3 --renderer vulkan --width 1568 --height 929 --root .

    # The test room.
    gtc-native-client --room-benchmark 60 --width 1568 --height 929 --renderer vulkan

The asset root (`--root`) is only a hint: if the skeleton is not there, the client
looks beside the executable and up from it, so a client started from `build/bin`
still finds the assets. `gtc-run.log` records which directory was used.

The skull benchmark prints the geometry cost separately from the draw, because a
backend can only own the second half.

The Vulkan figures are reported twice on purpose: **full frame** is draw, wait
for the device, and the picture copied back to host memory; **submit only** leaves
the result on the device. A backend that never copies its frame back -- OpenGL
present does not -- can only be compared against the second number. `setFrameReadback(false)`
is what the benchmark uses, and `GTC_VK_NO_COPY=1` does the same for a whole run.

Both benchmarks turn on `GTC_VK_PROFILE` themselves, so a Vulkan run prints the
frame broken into stages -- setup, record, submit and wait for the device, and the
readback -- for its first frames. When a backend is slow, that breakdown is the
only thing that says which stage to fix; `GTC_VK_PROFILE=1` does the same for a
normal run. `--renderer all` measures the software
rasterizer, Vulkan and OpenGL in one run; OpenGL needs a real window, so it is
measured through the window's own path. The interactive game does not print periodic frame-rate reports.

The room is the acceptance measurement, and it can be timed without a display:
`gtc-native-client --room-benchmark 60 --width 1568 --height 929 --renderer vulkan`
prints ms/frame and fps for the skeleton room at that size, refusing to run if the backend cannot
start rather than quietly measuring the CPU fallback. `--renderer scene` does the same with a
backend that accepts the scene and draws nothing, which reports the half of the frame that stays on
the CPU either way; on the two-core build machine that is 4.1 ms for the room at 1568x929 (the
batches are rebuilt at the pose's 15 Hz, not per frame) against 94.6 ms for the full CPU path.

Native controls: **1/2/3** select TPS/god/FPS; WASD moves or fast-pans god view; the player defaults to **6.4 km/h fast walking**, **Caps Lock** toggles **4 km/h slow walking**, and **Shift** sprints at **20 km/h** (or boosts god pan); NPCs use slow walking and fast walking for urgent trips. The default citizen is **1.7 m** tall. Click captures the mouse and Esc releases it; the wheel zooms. **T** zips/unzips male trousers, which start zipped with a loose, projecting two-panel cloth tent in every view. Unzipped **LMB** (or FPS **F**) pees; **RMB** aims the ranged weapon with modest zoom or blocks with melee. In god view, hold **Alt** to crouch; **RMB** paths, **Shift-RMB** auto-runs, **double-RMB** uses advanced jump/climb/crouch traversal, **LMB** attacks, and **double-LMB** gives melee an advanced approach; ranged double-LMB simply shoots. **E** opens the in-range Sit/Shit/Piss choice or stands, and **1/2/3** pick one. **C** opens/closes the skeleton room; use WASD and mouse to fly/look, Space/Left Ctrl for up/down, Shift for faster flight and wheel for a small forward/back move. Esc releases the mouse and a left click captures it again. **F1** opens the need controls, and **F12** saves a native PNG. The mode strip and the F1 panel are clickable when the mouse is free (Escape frees it): the block or
entry under the pointer brightens, and a click on it does what its key does -- the box that lights
up is the box that answers, the entry order is the key order, so entry 3 is the "4" beside it, and CLOSE at the foot of the panel is a button too. Pointing at the throne lights it up and puts **[E] Interact** and the object's name under the reticle as plain text. The interface on screen is five things, drawn by the host into the finished picture and rebuilt one piece at a time: a reticle at the middle of the window (dark-outlined, absent in god view, one pixel per 640 of the smaller side), the **F1** need panel (centred, translucent, two columns of keys with the current percentages), the mode bar along the bottom edge (`1 3RD PERSON  2 GOD  3 1ST PERSON`, the running mode ruled underneath), the **E** interaction circle at the throne (one disc cut into three equal sectors with a cancel button in the middle, the sector under the pointer lit, the middle click cancelling, nothing else on screen) and the **H** controls sheet, both click-tested from the same layout they were drawn with, the needs panel in the bottom right (PISS, SHIT and HP as three bars with their values, which steps above the mode bar on a narrow window rather than being drawn through it), and the frame-rate strip at the top. All text is the game's own proportional sans, antialiased, at least 20 pixels high and larger on larger windows. It is not a pixel font, because this is not that kind of game. The overlay that used to be here -- corner panels, name tags, crosshair, clock, minimap, notice bar and the three pop-up panels -- was deleted rather than repaired, and is being rebuilt one piece at a time on top of that box.

The server runs a 24-hour clock advancing at **15× real time**: one game day lasts 96 real minutes. The server replicates physical piss droplets and ranged rounds with velocity, wind, 9.81 m/s² gravity, swept surface/character/toilet collision, and impact damage. A 75% bladder provides 21 seconds of continuous **9 m/s** flow; a straight-up stream clears the default citizen's head. The crosshair raycasts the replicated contact geometry and the server converges physical droplets from the emission point, including an inner-rim-clearing arc into the measured water ellipse. Urine creates supported translucent yellow-green spots on floors, walls, object sides/tops, and toilet surfaces. A 100% bladder is 600 ml (0.714286 ml per droplet); the simple 1 mm film is only a geometry conversion, giving 0.6 m² at full emptying, and radius follows square-root area. Puddles store explicit millilitres and evaporation subtracts the rounded 20 °C/50% RH flux as 200 ml/m² per game hour, instead of deleting every spot at 24 hours. The measured toilet has a 381 mm seat, 743 mm tank, and an actually open bowl with water approximately 235 mm above the floor, rather than a tall invisible catch volume.

To check the interface as pixels without a display -- centring, the transparent blend, the reticle's arms and gap, the mode bar's three blocks and its rule, the needs panel's three bars, which have to agree with the numbers beside them, the antialiased ramp of the real face, the 20-pixel floor, both byte orders and four window sizes, with enlargements left in `build/native-client/interface-*.png`:

```sh
./build/bin/gtc-fps-counter-check
```

With the server running, execute the two-player test:

```sh
./build/bin/gtc-native-smoke --output build/native-smoke
```

It fails unless both clients replicate distinct identities, universal morph/style changes, server movement, a replicated path, and a server-granted seated Shit action. It writes `report.json` plus inspected **640×480 landscape** TPS, FPS, and god-view PNGs, including close toilet and zipped-cloth side views. The renderer uses the CC0 full-body mesh and rig weights, all replicated universal morphs, generic skin maps and MHCLO hair, near-plane clipping, camera obstruction handling, and authoritative puddles/actions. No portrait texture or person-specific sculpt is used.

## Portrait fitter

From the repository root:

```sh
./build/bin/gtc-native-fit --iterations 8
```

Fit one or more named presets, or render the authoritative native presets without fitting:

```sh
./build/bin/gtc-native-fit --render-only marilyn obama
./build/bin/gtc-native-fit --no-render xi
./build/bin/gtc-native-fit --direct --output build/native-direct marilyn
```

The review resolution is deliberately fixed at **480×640**. Outputs are written after each person to `build/native-fit/`:

- `NAME-initial.png` and `NAME-final.png` for visual A/B inspection;
- `results.json` for initial/final numeric error, evaluations, and timing;
- `presets.json` for the candidate universal values.

A candidate is **not** promoted automatically. Lower landmark error is not likeness acceptance; inspect the native renders and reject distortion, scalp/eye artifacts, or a worse silhouette.

## Inputs and method

- `native/presets.json` is the authoritative native source for the seven people and their camera poses. It contains only universal morph values and generic style choices.
- The model is read directly from the vendored CC0 MakeHuman OBJ, skeleton, and universal target files. No person-specific sculpt targets or portrait textures are used.
- Canonical landmark correspondences are built once per sex with bounded anisotropic alignment and closest-triangle barycentric mapping. Each trial evaluates deformed surface points and a perspective camera numerically: coarse pose search, bounded Levenberg–Marquardt with a prior penalty, then integer-slider refinement. `--direct` retains the nearest-vertex mapping for A/B regression checks.
- The CPU rasterizer has perspective-correct UV interpolation, a depth buffer, generic skin detail/normal maps, procedural eyes, MHCLO barycentric hair, alpha-tested generic hair textures, and a parametric scalp/hairline pass. Head/neck face selection uses the vendored CC0 skeleton weights rather than a person-specific cut.
- The reference landmark JSON is development fitting data only. It is never sampled as an image or transferred to a material.

The preserved browser implementation is a legacy checkpoint only. Native networking, GPU/CPU world rendering, interactive Win32/X11 window/input, the on-screen counter, and the headless multiplayer driver are implemented here. Actual native PNG review remains required for visual acceptance.

## Vendored decoder

`native/vendor/stb_image.h`, `stb_image_write.h`, and `stb_truetype.h` are from `nothings/stb`, commit `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5`. stb is dual-licensed under MIT or public domain; see `native/vendor/README.md`. These third-party headers are exempt from the authored-file line nagger. `native/assets/ui-sans.ttf` is unmodified DejaVu Sans; its Bitstream Vera/DejaVu redistribution notice is retained in `native/assets/FONT-LICENSE.txt`.

### P: one point, one plane, one tick

The physics rebuild starts from the smallest case that can be checked by eye.
Press P to enter the room, R to drop the point again, and hold the left mouse
button to move the point. The world contains one point with a mass of 1 kg and
one plane at y = 0.

- The only time step is the 20 Hz tick: one twentieth of a second. A velocity of
  v metres per second carries the point v/20 metres in a tick.
- Gravity is 9.8 m/s² downwards, so the force is m·g and the acceleration is
  F/m. The point's speed changes by that acceleration each tick; nothing else
  sets its velocity.
- Before the point is allowed to travel, the segment from where it is to where
  that speed would take it is walked. If the segment meets the plane, the point
  stops at the crossing. At 100 km/s one tick covers 5 km and the point is still
  stopped by the plane, because the plane is found before the step is taken.
- While the point rests on the plane, the plane pushes back with the point's
  weight, exactly cancelling gravity, so the net force is zero and the
  acceleration is zero. That is why the point stays put instead of sinking.
- A faint dot marks the position at each past tick, so the 20 Hz cadence and the
  shrinking gaps between ticks are visible. The blue line is the current tick's
  trace; it ends where the point was allowed to reach.
- Physics stays at 20 Hz and the screen is not: each frame draws a moving body
  between the position it had at the previous tick and the position the last
  tick left it in, at the fraction of a tick that frame has reached. The trace
  is drawn across the same two positions, so the point on screen always sits on
  the line that was walked. This is Minecraft's partial-tick interpolation; it
  changes only what is drawn, never what is simulated.
- Each body decides this for itself. A frozen body is drawn exactly where it is,
  and the body the mouse is holding is drawn exactly where the mouse put it, but
  neither of those may switch the interpolation off for anything else.

### The bodies: points at corners

There are two solids in the room, and they are the same thing at different
sizes: a body is a set of corner points of equal mass, 1 kg each, plus the
distances between those points that are not allowed to change. Nothing in the
room asks which body it is looking at, so both go through one piece of code.

- The cube: a 0.70 m square made of four points, one at each corner. Six
  distances are held -- four edges and two diagonals -- because a square can fold
  into a diamond without the diagonals.
- The triangle: a right isosceles triangle with two 2.10 m legs and a 2.97 m
  hypotenuse, made of three points, one at each corner. That is three times the
  cube's side, exactly as asked. It stands on its two base corners, and a
  triangle is already rigid with its three edges, so it holds three distances.

- Each corner gets gravity on its own and is asked to travel its own v/20.
- Each corner traces its own segment against the plane before it is allowed to
  move, so there is one hitscan per corner, not one per body.
- When one corner moves, the others are affected through the distances that are
  not allowed to change. Every distance is pushed back to its original length,
  splitting the error between its two corners, with the plane clamp applied
  between passes so satisfying a distance cannot push a corner through the
  floor.
- A corner the plane is holding cannot be pushed down into it, so any distance
  that wants to move such a corner downwards puts that motion into the corner at
  the other end instead. It is still free to slide along the floor. Sharing the
  correction equally instead is what lets a tall body balance in mid-air on a
  corner the floor cannot carry.
- The distances are swept forwards and then backwards every pass. Pushing them
  back in one fixed order lets the order itself move the body, and over a second
  that steady lean adds up.
- A corner the plane is holding cannot still be travelling into it, so its
  downward speed is removed at the end of the tick.
- A body is frozen only when its weight passes between the corners that are
  touching the floor, so nothing is ever frozen balanced on a single corner.

The shape is not approximated. Each tick, the distances and the plane are
solved together -- push the distances straight, put any corner that went through
the plane back on it, check both again -- until neither is out by more than a
nanometre. The order matters: the plane is what opens the distance error, so the
distance solve has to see the plane. What is drawn is then rebuilt from where the
body is and how far round it has turned, never by sliding four corners along
straight lines between ticks, so a turning body cannot look squashed. A corner the plane holds loses its
downward velocity, and a constraint is not allowed to hand a corner more speed
than it already had, so the push-back and the plane cannot pump energy into a
body. The worst distance error measured across every run is 0.00000 mm.

### Friction

A contact answers only the motion that goes into the surface. Everything running
along the surface is the body's own kinetic energy, and it is friction that
takes it away -- as a force, over time, never in an instant. Deleting the
sideways velocity at the moment of contact is not a contact, it is an energy
sink, and it made a dot that had been accelerated down a slope by gravity stop
dead the instant it touched the floor.

Two coefficients, as asked:

- Static: a surface holds a body still while `mu_s * N` is enough to answer the
  pull on it. Below that, nothing moves at all.
- Kinetic: once it is sliding, `mu_k * N` takes speed away tick by tick,
  `v -= mu_k * g * dt`.

`N` is the part of the weight the surface carries, so a 45 degree face grips less
than the floor does and the point keeps most of the speed it built falling. A
surface answers only the force that drives into it: the rest still acts on the
point, so on a slope there is a real force down the slope and the point
accelerates down it at `g * sin(theta)`. Static friction holds a point still only
while it is being pushed weakly enough and is not already moving; on a 45 degree
face `mu_s * N` is nowhere near enough, so it slides, and that is the whole
difference between sliding and creeping. The
point is smooth: `mu_s` 0.40, `mu_k` 0.30, so it runs a long way and then stops.
Each solid carries its own pair, 0.70 and 0.55, and that coefficient is scaled by
the length of the contact it actually has with the floor -- a body standing on a
longer footprint grips proportionally harder. Friction on a solid is applied to
the body, not to each corner: the floor has already pinned the corners it holds,
so rubbing them one at a time does nothing at all.

Measured: the dot slides off the triangle at 4.8 m/s and keeps it, running
another 1.45 m along the floor before friction stops it; a dot pushed at 0.10 m/s
moves 7 mm and is held, one pushed at 3 m/s slides 1.76 m. A 12 m/s strike moves
the cube 43 mm and the taller triangle only 33 mm, and neither slides on
afterwards.

### Freezing

A body with no velocity and no net force is frozen and not stepped at all. The
tick still counts, since it has already passed, but there is no force, no
motion and no contact work for a frozen body. Freezing also ends the drawing
interpolation for that body, so a frozen body is drawn exactly where it is
instead of part of the way along a tick. R resets; dragging the point wakes it.

### The point and the bodies

Before the point travels, its segment is walked against every body's edges as
well as the plane, and whichever it reaches first is where it stops. A hit on a
body exchanges momentum: the point carries m·v, the corner it struck
takes half of the impulse and the other three corners share the rest, so the
square as a whole gains exactly what the point loses and is set turning by the
uneven share. The square pushes back along the face normal and takes the whole
approach speed, giving none of it back -- the same thing the plane does to the
point. Nothing bounces off anything in this room: a contact stops what ran into
it and passes the momentum on.

A point that has spent its speed on a body sits on the face that stopped it,
exactly where it would sit on the plane, and freezes there on the same rule:
no velocity, no net force. It is held up by the square and by nothing else, so
taking the body away -- dragging it out from under the point, or letting it
slide -- drops the point back into free fall. A drop onto the plane and the same
drop onto a flat face give the same trace: the falling speed builds tick by tick
and the last tick ends at rest, not at a bounce.

The impulse goes along the contact normal, not straight down. A dot dropped from
a great height has all of its momentum pointing down, but the face it land on
turns that into a push along the normal: onto the triangle's 45 degree face it
leaves at exactly -45 degrees, along the face, and the face is pushed the other
way. A body standing on the plane cannot be pushed down into it, but the plane
only answers the part of the blow that drives into it -- the part that runs
along the plane has to go into the body. That is why a drop on the triangle
shoves the triangle sideways instead of being swallowed by the floor.

A body whose face is flat and horizontal is a different matter: the whole blow
drives straight down into the plane, the plane answers all of it, and the body
does not move -- which is the same reason it does not hop. A drop from 20 m onto
the square's top face moves the square 0.0000 mm; the same drop onto the
triangle's slope moves it 81 mm sideways at the impulse, and hitting a corner of
the square moves the square 25 mm sideways.

The tick's travel is walked from contact to contact: the segment is tested
against every body and the plane, the point is moved to the first thing it
meets, that contact is answered, and it carries on with the time that is left,
for up to four contacts. Nothing is passed through however fast the point goes,
and it never loses speed it did not spend on a contact. A point left a hair
inside a body is put back onto the nearest point of its boundary rather than
nudged out along an edge, because nudging leaves it a centimetre deep and it
creeps along the inside of the face it is stuck in.

What is left of a tick after a contact is walked with the speed the contact left
behind. That is what lets the point run down the triangle's 45 degree face
instead of being parked at the contact and creeping, and the walk stops at
anything it runs into, so nothing is passed through.

Because the crossing is found before the point moves, none of this depends on
how fast the point was going. A point arriving at 100 km/s is stopped at the
surface in the same tick.

### The ball

A basketball is in the room too: 0.24 m across, 0.62 kg, drawn as one disc. It is
not built from corner points like the solids and nothing is sampled on its
surface -- it is one point with a radius, and that radius is all the shape it
has. There is one piece of ball code and no shape is named anywhere in it: the
cube and the triangle are the same two-or-more-sided outline, and both are met by
the same test.

- A contact is found before the ball moves, and the test is exact. A flat side is
  met where the ball's own surface would reach it, so the side is looked at one
  radius further out and not where the ball's middle would cross it; a corner is
  met when the middle is one radius from it. Both cases cover a circle against a
  polygon completely, so nothing is stepped or guessed.
- The other side's motion is in the test. Taking the body's travel off the
  ball's leaves the same question -- do these two things meet -- and because a
  body's corners travel in straight lines within a tick, the answer stays exact.
  This is what finds a body falling onto a ball that is lying still: the ball's
  own travel cannot, because there is none. The ball is stepped before the bodies
  for the same reason. The test is run whichever of the two is awake.
- The ball turns as well as travels. Friction acts at the bottom of the ball, one
  radius from its middle, so the same force that slows it also spins it: the
  moment of inertia of a solid sphere is `0.4 * m * r^2`, and the grip is
  limited by `mu_k * N` like everywhere else.
- The spin is not assumed. The contact answers the slip at the bottom of the
  ball, so a ball dropped flat starts skidding, the skid drives the spin up, and
  it settles into rolling. From there rolling resistance -- 0.015 of gravity --
  slows the travel and the spin by the same proportion, so a roll cannot turn
  back into a skid on its own.
- A ball touches things all the way round. A body lying on a ball is touching the
  ball's top; the ball on top of the cube is touching the cube's face; a ball
  resting exactly on a face is at the surface and not outside it, so that counts
  as touching too. There is no preferred side in the code, and the measurements
  below are all eight approaches: both faces, both corners and the top of each
  body, and the underside.
- Bouncing is the ball's own: it gives back 0.75 of the speed it lands with, so
  it keeps 56 percent of the height of a bounce. Nothing else in the room bounces
  and nothing else got a bounce -- the solids still take the whole approach
  speed, as before. Below 1 m/s there is no bounce left to give, which is what
  lets a ball lie still instead of trembling.
- Air acts on it: `0.5 * rho * Cd * A * v^2` against the direction of travel,
  with a sphere's drag coefficient. A hard knock decays instead of rolling out of
  the room for ever.
- A ball squeezed between the floor and a face coming down on it cannot stay and
  cannot go down, so it leaves sideways at the speed of the thing pressing it.
  That is the only answer that is not a teleport with no momentum; held by the
  mouse it is placed rather than squeezed, so it is just put outside.
- `R` puts a fresh ball back where it started, and `R` is where the ball starts:
  0.28 m up, dropped, bouncing and coming to rest at exactly its radius.

Measured: 4.8 m/s onto the floor rebounds at 4.3 m/s and peaks at 1.15 m; a ball
dropped on the cube's top rests at 0.82 m (0.70 of cube plus 0.12 of ball); a
point dropped on a resting ball rests on it at 0.31 m (ball, its radius, and the
dot's); rolling off the triangle's slope into the cube's side moves the cube by
29 mm and stops; the closest approach between the ball and any body in every one
of those runs is 0.0000 m, meaning touching and never inside.

### Grabbing

The left mouse button takes whatever is under the ray: the point, the cube, the
triangle or the ball. A click near the point takes the point even when it is sitting
on a body, so it can always be picked up off a face. A body is taken by its
middle, so all of its corners move by the same amount and none of the distances
it is made of are asked to stretch; it is placed rather than simulated while it
is held, and released with no velocity. Dragging the point still moves only the
point.

The room draws through the same Vulkan backend as everything else. Body
positions reach the device as batch model matrices, so nothing is uploaded or
read back per frame. There is one tick per 20 Hz interval and no hidden extra
update.
