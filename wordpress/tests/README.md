# Engine comparison

Checks that the browser engine (`dub-echo/assets/js/dub-echo-engine.js`) renders the same
audio as the plugin's C++ engine (`Source/dsp`). Run from this folder:

```
g++ -O2 -std=c++17 -I../../Source engine_compare.cpp ../../Source/dsp/TapeEchoEngine.cpp -o engine_compare
./engine_compare            # writes in.f32 and cpp.f32
node engine_compare.js      # renders the same input in JS and prints the difference per second
```

Expected: difference at least ~40 dB below the signal in every second (the only differences
are float vs. double rounding).
