# CalcGD

Geometry Dash for TI-84 Plus CE, rewritten in C using the CE C/C++ toolchain.

Based on [GeometryDashCE](https://github.com/MathisLav/GeometryDashCE) by Epharius and Anonyme0.

## building

Requires the [CE C/C++ Toolchain](https://github.com/CE-Programming/toolchain/releases).

```
export CEDEV=/path/to/CEdev
make
```

Output: `bin/CALCGD.8xp`

## running

Transfer to your calculator (or CEmu) along with:
- `ref/bin/GDGrphc.8xv` (game graphics)
- `ref/bin/GDMenu.8xv` (menu graphics)
- Level files from `ref/bin/levels/`

On OS 5.5+, you need [arTIfiCE](https://yvantt.github.io/arTIfiCE/) installed first.

## controls

- **2nd**: jump / fly (spaceship)
- **enter**: pause
- **left/right**: navigate menu
- **clear/mode**: quit
- **+**: create new level
- **alpha**: edit selected level

## license

Original game: CC BY-NC-SA 4.0
