#!/bin/sh
# Compila o descodificador libwebp (so leitura) com nxdk -> nslibs/lib/libwebp.lib
# Requer: eval "$(nxdk/bin/activate -s)"
set -e
P=/home/claude/netsurfx; W=$P/third_party/libwebp
mkdir -p $W/obj; rm -f $W/obj/*.obj
SRCS="$(ls $W/src/dec/*.c) $(ls $W/src/dsp/*.c | grep -v '_enc\|enc_\|cost\|ssim') \
 $W/src/utils/bit_reader_utils.c $W/src/utils/color_cache_utils.c $W/src/utils/filters_utils.c \
 $W/src/utils/huffman_utils.c $W/src/utils/quant_levels_dec_utils.c $W/src/utils/random_utils.c \
 $W/src/utils/rescaler_utils.c $W/src/utils/thread_utils.c $W/src/utils/utils.c $W/src/utils/palette.c"
for f in $SRCS; do
  o=$W/obj/$(echo $f | sed "s#$W/src/##; s#/#_#g; s#\.c\$#.obj#")
  nxdk-cc -O2 -std=gnu99 -include $P/nsshim/xbox_compat.h -DXBOX -DHAVE_CONFIG_H -I$P/nsshim -I$W -I$W/src -Werror=implicit-function-declaration -c $f -o $o
done
nxdk-lib /out:$P/nslibs/lib/libwebp.lib $W/obj/*.obj
mkdir -p $P/nslibs/include/webp && cp $W/src/webp/decode.h $W/src/webp/types.h $P/nslibs/include/webp/
