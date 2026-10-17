# Проверка на обычном Linux без vitasdk:
#   make test    - тесты логики + синт в out/synth_demo.wav + превью экрана out/*.ppm
#   make check   - проверка компиляции кода для Vita (main.c, audio.c) с заглушками SDK
CC ?= gcc
CFLAGS = -std=gnu99 -Wall -Wextra -Wno-unused-parameter -O2 -Istub
SRC = ../src
HDR = $(wildcard $(SRC)/*.h) $(wildcard stub/*.h stub/*/*.h)   # пересборка и при смене заголовков

all: test

out:
	mkdir -p out

tests: tests.c $(SRC)/theory.c $(SRC)/strum.c $(SRC)/synth.c $(HDR) | out
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^) -lm

preview: preview.c vita2d_host.c $(SRC)/ui.c $(SRC)/canvas.c $(SRC)/theory.c $(SRC)/strum.c $(HDR) | out
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^) -lm

check:
	$(CC) $(CFLAGS) -Werror -fsyntax-only $(SRC)/main.c
	$(CC) $(CFLAGS) -Werror -fsyntax-only $(SRC)/audio.c
	$(CC) $(CFLAGS) -Werror -fsyntax-only $(SRC)/ui.c $(SRC)/canvas.c $(SRC)/synth.c $(SRC)/theory.c $(SRC)/strum.c

test: check tests preview
	./tests
	./preview

clean:
	rm -rf tests preview out
