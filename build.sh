CFLAGS="-Wall -Werror -g -Wno-unused-variable -Wno-unused-function -Wno-missing-braces -std=c11"
LLIBS="-lm"

cc $CFLAGS $LLIBS *.c -o test
