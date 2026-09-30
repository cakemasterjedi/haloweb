HEVC (H.265) decoder compiled to WebAssembly, from hevc.js
(https://github.com/privaloops/hevc.js, MIT licence, see LICENSE). The emblem
serves these at /hevc.js and /hevc.wasm so the page can read phone videos in
browsers that can't play HEVC themselves (Firefox).

Built from commit cca2aea with tiles-fix.patch applied: the decoder didn't reset
the CABAC contexts at the start of each tile, so videos coded with tiles
(e.g. Samsung phones) only decoded their first tile. With the patch a Samsung
clip decodes bit-exact against ffmpeg, and the project's conformance tests
still pass.

Rebuild (Emscripten 6.0.8):

    git clone https://github.com/privaloops/hevc.js && cd hevc.js
    git checkout cca2aea && git apply tiles-fix.patch
    emcmake cmake -B build-wasm -DBUILD_WASM=ON -DCMAKE_BUILD_TYPE=Release
    cmake --build build-wasm --parallel
    cp build-wasm/hevc-decode.js build-wasm/hevc-decode.wasm <this folder>
