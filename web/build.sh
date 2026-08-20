#!/bin/sh
# Build the WASM verifier into docs/. Needs emscripten and a doodleink
# checkout (or use the .pio/libdeps copy).
set -e
cd "$(dirname "$0")"
DOODLEINK="${DOODLEINK:-../../doodleink/src}"
em++ -O2 -fno-rtti -fno-exceptions soul_wasm.cpp \
  -I"$DOODLEINK" -I../src \
  -s MODULARIZE=1 -s EXPORT_NAME=createDoodle -s ENVIRONMENT=web \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s EXPORTED_FUNCTIONS=_soul_from_mac,_soul_name,_soul_card,_skin_count,_skin_name,_frame_buf,_frame_w,_frame_h,_render,_malloc,_free \
  -s EXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString \
  -o ../docs/doodle.js
echo "built docs/doodle.js + docs/doodle.wasm"
