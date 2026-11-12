# Hoverstrike for Vircon32

A glowing-vector shooter for the Vircon32 console where you pilot a saucer against four rows of tanks. Its rules follow the 1983 Atari 2600 cartridge as documented in Dennis Debro's commented disassembly.
The code, shapes, sounds and names are original. The original's pilot mascot isn't used: a grounding gets its own crash scene. Lines are drawn with the project's shared `vector.h` library.

## Playing

Load `bin/Hoverstrike.v32` in the Vircon32 emulator or the RetroArch Vircon32 core.

| Control | Action |
|---|---|
| Left / Right (hold) | Scroll the landscape and tanks under the saucer |
| Up / Down | Climb and descend, within the hover limit |
| A or B | Fire the laser |
| Start | Start / pause |
| Title screen | Left/Right picks Game 1–4 (the starting speed) |

The saucer never moves sideways. You line tanks up beneath it by scrolling the ground, then fire.
The laser sweeps down through the four tank rows one per tick, and destroys the first tank it finds underneath.

Each tank missile that strikes you lowers your hover limit and pushes you down. When the saucer is pushed to the ground the game is over; there are no lives. The notches at the top show how many more strikes you can take from your current height.

## Taken from the disassembly

- **The saucer** sits at fixed x 56, and its laser comes down at x 60. It starts at height 16, the hover limit begins there too, and the ground is at 40.
- **Scrolling:** holding Left or Right moves every tank and the missile 2 units per tick (`TANK_SCROLL_SPEED`). The direction is cleared every frame (`$1150`).
- **Tank rows** are at 48 / 54 / 60 / 67, and the laser reaches 53 / 59 / 66 / 69 at each step.
- **Tank movement** (`$0869`):
  - The tank chosen by the score's last two digits (& 3) runs at 2, or at 3 when the hundreds digit is odd. The others run at 1.
  - With an odd score they all run left, one step faster.
  - They step on ticks where `tick & TankFrameDelayValues[speed]` is 0.
- **Tanks enter the field** from the right edge, and only while you're not scrolling that way. All four start stacked together, exactly as in the cartridge, and spread out as the score changes their speeds.
- **Tank missile:**
  - One at a time, from each tank in turn, and only during alternate 64-tick spells.
  - It flies up and to the right at the game speed.
  - It disappears once it's 8 short of the right edge, or above height 19, so a saucer at the very top is hard to hit.
- **A strike** sets a 31-tick hit animation. After it, the hover limit and the saucer each drop by 4.
- **Rack scoring:** 53, 42, 31 and 20 points for the first to last tank, 146 a rack. The score tops out at 9,999.
- **Speed by rack:** the game's starting speed, +1 on racks 4–7 and 12–15, and +2 on rack 8 of every 16.

## My own choices

- **Smoother motion:** logic runs on alternate frames like the cartridge, but motion is spread across both frames.
- **Speeds 5 and 6:** the cartridge reads past the end of its tank delay table here, into unrelated data. I treat both as "move every tick".
- **Missiles disappear on a hit.** In the cartridge one could keep flying after striking.
- **Visuals:** the mountains scroll at half the speed of the ground, as parallax.

## Building

You need Python 3 with NumPy and Pillow, and the Vircon32 dev tools on your PATH. Then:

```
./build.sh
```
