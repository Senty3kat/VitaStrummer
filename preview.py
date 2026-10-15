name: Build VPK

on:
  push:
  workflow_dispatch:

jobs:
  host-check:
    # быстрая проверка логики на обычном Linux: тесты аккордов/струн, синт в WAV, превью экрана
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Host tests
        run: make -C host test
      - uses: actions/upload-artifact@v4
        with:
          name: host-preview
          path: |
            host/out/*.ppm
            host/out/*.wav

  build:
    runs-on: ubuntu-latest
    container: vitasdk/vitasdk:2026.08
    steps:
      - uses: actions/checkout@v4
      - name: Build
        run: |
          cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
          cmake --build build
      - uses: actions/upload-artifact@v4
        with:
          name: Strummer-vpk
          path: build/Strummer.vpk
