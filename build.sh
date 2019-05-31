CFLAGS="-Wall -Werror -g -Wno-unused-variable -O2 -Wno-unused-function -Wno-missing-braces -std=c11"
LLIBS="-lm"

cc $CFLAGS $LLIBS *.c -o nua
