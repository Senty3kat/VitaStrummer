/* Проверки логики без консоли: аккорды, зоны струн, стрaм, синт (пишет WAV для прослушивания) */
#include "../src/theory.h"
#include "../src/strum.h"
#include "../src/synth.h"
#include "../src/layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int hits[64], nhits;
static float fracs[64];
static void on_hit(int zone, float frac, void *u) { (void)u; if (nhits < 64) { fracs[nhits] = frac; hits[nhits++] = zone; } }

static void test_theory(void) {
    CHECK(chord_mask(0, 0) == ((1 << 0) | (1 << 4) | (1 << 7)));          /* C maj */
    CHECK(chord_mask(9, chord_type(1, 0)) == ((1 << 9) | (1 << 0) | (1 << 4)));   /* A min */
    CHECK(chord_mask(0, chord_type(0, 7)) == ((1 << 0) | (1 << 5) | (1 << 7)));   /* Csus4 */
    CHECK(chord_mask(0, chord_type(0, 8)) == ((1 << 0) | (1 << 4) | (1 << 8)));   /* Caug */
    CHECK(chord_mask(11, chord_type(1, 8)) == ((1 << 11) | (1 << 2) | (1 << 5))); /* Bdim */
    CHECK(snap_to_mask(61, chord_mask(0, 0)) == 60);                       /* C# -> C (вниз при равенстве нет: 1 и 3) */
    CHECK(snap_to_mask(62, chord_mask(0, 0)) == 60);                       /* D: C на 2, E на 2 -> нижний */
    CHECK(snap_to_mask(66, chord_mask(0, 0)) == 67);                       /* F# -> G */

    char b[16];
    chord_name(b, sizeof(b), 9, chord_type(1, 1)); CHECK(strcmp(b, "Am7") == 0);
    chord_name(b, sizeof(b), 0, chord_type(1, 0)); CHECK(strcmp(b, "Cm") == 0);
    chord_name(b, sizeof(b), 0, chord_type(1, 2)); CHECK(strcmp(b, "CmM7") == 0);
    chord_name(b, sizeof(b), 5, chord_type(0, 7)); CHECK(strcmp(b, "Fsus4") == 0);
    chord_name(b, sizeof(b), 7, chord_type(0, 4)); CHECK(strcmp(b, "G9") == 0);
    CHECK(strcmp(type_label(chord_type(1, 3)), "m6") == 0 && strcmp(type_label(chord_type(0, 3)), "6") == 0);
    chord_name(b, sizeof(b), 6, 0); CHECK(strcmp(b, "F#") == 0);
    note_name(b, sizeof(b), 61);   CHECK(strcmp(b, "C#4") == 0);
    note_name(b, sizeof(b), 84);   CHECK(strcmp(b, "C6") == 0);

    StringMap m;
    stringmap_build(&m, 0, 0, 0, 60);
    CHECK(m.nzones == N_STRINGS);
    CHECK(m.note[0] == 60 && m.note[24] == 84);

    /* в любом аккорде: не больше двух струн подряд на звук, только звуки аккорда, строго вверх,
     * диапазон шире клавиатуры (у трезвучий - больше 4 октав) */
    for (int root = 0; root < 12; root++)
        for (int t = 0; t < N_TYPES; t++) {
            stringmap_build(&m, 1, root, t, 60);
            int mask = chord_mask(root, t), iv[MAX_CHORD];
            CHECK(m.nzones == (N_STRINGS + CHORD_SPREAD - 1) / CHORD_SPREAD);
            for (int z = 0; z < m.nzones; z++) {
                CHECK(m.zone_hi[z] - m.zone_lo[z] + 1 <= 2);
                CHECK(mask & (1 << (m.zone_note[z] % 12)));
                if (z) CHECK(m.zone_note[z] > m.zone_note[z - 1]);
            }
            int span = m.zone_note[m.nzones - 1] - m.zone_note[0];
            CHECK(span > 24);
            if (chord_intervals(t, iv) == 3) CHECK(span >= 45);     /* трезвучия - больше 4 октав */
            CHECK(m.zone_note[0] >= 40 && m.zone_note[m.nzones - 1] <= 108);   /* слышимый диапазон */
        }
    stringmap_build(&m, 1, 0, 0, 60);
    printf("C major:");
    for (int z = 0; z < m.nzones; z++) { note_name(b, sizeof(b), m.zone_note[z]); printf(" %s", b); }
    printf("\nAm7:    ");
    stringmap_build(&m, 1, 9, chord_type(1, 1), 60);
    for (int z = 0; z < m.nzones; z++) { note_name(b, sizeof(b), m.zone_note[z]); printf(" %s", b); }
    printf("\n");

    int nt[MAX_CHORD], n = pad_voicing(7, chord_type(0, 1), nt);    /* G7 */
    CHECK(n == 4 && nt[0] == 43 && nt[3] == 53);
}

static void test_strum(void) {
    StringMap m;
    stringmap_build(&m, 0, 0, 0, 60);
    StrumFinger f;

    /* касание - звучит струна под пальцем */
    nhits = 0;
    strum_down(&f, 1, strum_pos_from_xy(800, strum_string_y(3)), &m, on_hit, NULL);
    CHECK(nhits == 1 && hits[0] == 3);

    /* быстрый свайп вверх через всю панель за один кадр: каждая струна ровно один раз, доли кадра растут */
    nhits = 0;
    strum_move(&f, strum_pos_from_xy(800, STR_TOP + 1), &m, on_hit, NULL);
    CHECK(nhits == 21);
    for (int i = 0; i < nhits; i++) CHECK(hits[i] == 4 + i);
    for (int i = 1; i < nhits; i++) CHECK(fracs[i] > fracs[i - 1]);

    /* палец дрожит на границе - без повторов */
    stringmap_build(&m, 0, 0, 0, 60);
    nhits = 0;
    float b = 10.0f;
    strum_down(&f, 2, b - 0.05f, &m, on_hit, NULL);
    for (int i = 0; i < 20; i++) strum_move(&f, b + ((i & 1) ? 0.08f : -0.08f), &m, on_hit, NULL);
    CHECK(nhits == 1);

    /* в аккорде зона из нескольких струн звучит один раз */
    stringmap_build(&m, 1, 0, 0, 60);
    nhits = 0;
    strum_down(&f, 3, 0.5f, &m, on_hit, NULL);
    strum_move(&f, 24.5f, &m, on_hit, NULL);
    CHECK(nhits == m.nzones);
}

static void test_keys(void) {
    /* 15 белых и 10 чёрных, центры клавиш идут строго вверх с ростом ноты */
    int nb = 0;
    for (int i = 0; i < N_STRINGS; i++) nb += key_is_black(i);
    CHECK(nb == 10);
    for (int i = 1; i < N_STRINGS; i++) CHECK(key_center_y(i) < key_center_y(i - 1));

    /* у белой клавиши задняя часть не шире передней и не залезает на чёрные */
    for (int i = 0; i < N_STRINGS; i++) {
        if (key_is_black(i)) continue;
        float y0, y1, b0, b1;
        white_key_extents(i, &y0, &y1, &b0, &b1);
        CHECK(b0 >= y0 && b1 <= y1 && b1 > b0);
        if (i + 1 < N_STRINGS && key_is_black(i + 1)) CHECK(b0 > key_center_y(i + 1) + BK_H / 2.0f);
        if (i > 0 && key_is_black(i - 1)) CHECK(b1 < key_center_y(i - 1) - BK_H / 2.0f);
    }

    /* касание по центру клавиши и по центру струны даёт ту же струну */
    for (int i = 0; i < N_STRINGS; i++) {
        float xk = key_is_black(i) ? (BK_X0 + BK_X1) / 2.0f : (WK_XF + WK_X1) / 2.0f;
        CHECK((int)strum_pos_from_xy(xk, key_center_y(i)) == i);
        CHECK((int)strum_pos_from_xy((BULB_X1 + BAR_X1) / 2.0f, strum_string_y(i)) == i);
    }

    /* движение вбок с клавиш на струны: положение меняется плавно, без скачков */
    for (float y = KB_TOP; y <= KB_BOTTOM; y += 7.0f) {
        float prev = strum_pos_from_xy(BK_X0, y);
        for (float x = BK_X0; x <= BAR_X1; x += 2.0f) {
            float p = strum_pos_from_xy(x, y);
            CHECK(fabsf(p - prev) < 0.1f);
            prev = p;
        }
    }
}

static void wav_write(const char *path, const int16_t *s, int n) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t data = (uint32_t)n * 2, riff = 36 + data, rate = SYNTH_RATE, br = SYNTH_RATE * 2, fmt = 16;
    uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(s, 2, (size_t)n, f);
    fclose(f);
}

/* звук: хроматический свайп, стрaм C и Am7, затем пэд C -> G7 -> Am7 -> тишина */
static void test_synth(void) {
    const int G = 256, total = SYNTH_RATE * 16;
    int16_t *out = calloc((size_t)total, 2);
    synth_init();
    int pos = 0, clip = 0;
    StringMap m;
    for (int blk = 0; pos + G <= total; blk++, pos += G) {
        float t = (float)pos / SYNTH_RATE;
        /* каждые ~16 мс "кадр тача" - раз в 3 блока */
        if (blk == 10) { stringmap_build(&m, 0, 0, 0, 60); for (int i = 0; i < 25; i++) synth_strum(m.note[i], i * 400); }
        if (blk == 300) { stringmap_build(&m, 1, 0, 0, 60); for (int z = 0; z < m.nzones; z++) synth_strum(m.zone_note[z], z * 600); }
        if (blk == 470) { stringmap_build(&m, 1, 9, chord_type(1, 1), 60); for (int z = m.nzones - 1, k = 0; z >= 0; z--, k++) synth_strum(m.zone_note[z], k * 500); }
        if (blk == 700) { int nt[6], n = pad_voicing(0, 0, nt); synth_pad(nt, n); }
        if (blk == 1000) { int nt[6], n = pad_voicing(7, chord_type(0, 1), nt); synth_pad(nt, n); }
        if (blk == 1250) { int nt[6], n = pad_voicing(9, chord_type(1, 1), nt); synth_pad(nt, n); }
        if (blk == 1500) synth_pad(NULL, 0);
        (void)t;
        synth_render(out + pos, G);
        for (int i = 0; i < G; i++) if (out[pos + i] >= 32000 || out[pos + i] <= -32000) clip++;
    }
    CHECK(synth_active_voices() == 0);
    printf("synth: clipped samples %d of %d\n", clip, total);
    wav_write("out/synth_demo.wav", out, pos);
    free(out);
}

int main(void) {
    test_theory();
    test_strum();
    test_keys();
    test_synth();
    printf(fails ? "\n%d FAILED\n" : "\nall tests passed\n", fails);
    return fails ? 1 : 0;
}
