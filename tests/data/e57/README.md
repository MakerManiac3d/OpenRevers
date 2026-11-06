# E57 test files

Written with the reference library [libE57Format](https://github.com/asmaloney/libE57Format)
3.4.1 by `tools/make_e57_fixtures.cpp`, so that OpenRevers' own E57 reader is
checked against files it did not write itself. The points follow a formula
(a Fibonacci spiral on a sphere) that `tests/test_e57.cpp` repeats.

| File | Content |
| --- | --- |
| `float_normals.e57` | 8 000 points, Cartesian single-precision floats, radius 50 m, with `nor:` normals; several data packets |
| `scaled_pose.e57` | 6 000 points, Cartesian scaled integers (0.1 mm), radius 40 m, every tenth point invalid, intensity and colour; pose: 90° about z and 100 m along x |
| `spherical_two_scans.e57` | Two scans of 3 000 points, spherical coordinates in doubles, radius 30 m; the second scan moved 200 m along y |

To make them again:

```sh
g++ -std=c++17 -I libE57Format/include -I libE57Format/build tools/make_e57_fixtures.cpp \
    libE57Format/build/libE57Format.a -lxerces-c -lpthread -o make_e57_fixtures
./make_e57_fixtures tests/data/e57
```
