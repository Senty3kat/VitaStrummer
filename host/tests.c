/* Проверки логики без консоли: аккорды, зоны струн, стрaм, синт (пишет WAV для прослушивания) */
#include "../src/theory.h"
#include "../src/strum.h"
#include "../src/ui.h"
#include "../src/synth.h"
#include "../src/layout.h"
#include <stdio.h>
#include <time.h>
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

/* гитара: тремоло - каждое пересечение линии струны звучит, дрожание у линии - нет */
static int g_ghits[16], g_gn;
static float g_gfr[16];
static void gtr_hit(int zone, float frac, void *u) { (void)u; if (g_gn < 16) { g_gfr[g_gn] = frac; g_ghits[g_gn++] = zone; } }
static void test_gtr_tremolo(void) {
    StrumFinger f;
    memset(&f, 0, sizeof(f));
    float c = gtr_string_y(2), x = 800.0f, y = c - 12.0f;     /* над третьей струной, в её полосе */
    g_gn = 0;
    f.x = x; f.y = y;
    strum_gtr_down(&f, 1, x, y, gtr_hit, NULL);
    CHECK(g_gn == 1 && g_ghits[0] == 2);                        /* касание - струна под пальцем */
    /* сразу вниз через линию - это тот же удар, второго звука нет */
    float path[] = { c + 10.0f, c - 10.0f, c + 10.0f, c - 10.0f, c + 10.0f };
    for (int i = 0; i < 5; i++) { strum_gtr_move(&f, x, path[i], gtr_hit, NULL); f.y = path[i]; }
    for (int k = 0; k < 4; k++) { strum_gtr_move(&f, x, f.y, gtr_hit, NULL); }   /* постоять */
    printf("tremolo: %d hits\n", g_gn);
    CHECK(g_gn >= 4);                                           /* туда-обратно на 20 px - удар каждый раз */
    for (int i = 0; i < g_gn; i++) CHECK(g_ghits[i] == 2);
    int n0 = g_gn;
    float jit[] = { c + 7.0f, c + 3.0f, c + 6.0f, c + 2.0f, c + 8.0f };   /* дрожание у линии */
    for (int i = 0; i < 5; i++) { strum_gtr_move(&f, x, jit[i], gtr_hit, NULL); f.y = jit[i]; }
    CHECK(g_gn == n0);
    /* палец поставлен между струнами - тишина; вниз через линию - звук, вверх - снова звук */
    g_gn = 0;
    f.x = x; f.y = c - 30.0f;
    strum_gtr_down(&f, 2, x, f.y, gtr_hit, NULL);
    CHECK(g_gn == 0);
    strum_gtr_move(&f, x, c + 10.0f, gtr_hit, NULL); f.y = c + 10.0f;
    CHECK(g_gn == 1);
    strum_gtr_move(&f, x, c - 10.0f, gtr_hit, NULL); f.y = c - 10.0f;
    CHECK(g_gn == 2 && g_ghits[0] == 2 && g_ghits[1] == 2);
    /* проведение через все струны - каждая по разу, по порядку */
    g_gn = 0;
    f.y = gtr_string_y(0) + 20.0f;
    strum_gtr_move(&f, x, f.y, gtr_hit, NULL);
    g_gn = 0;
    strum_gtr_move(&f, x, gtr_string_y(5) - 20.0f, gtr_hit, NULL);
    CHECK(g_gn == 6);
    for (int i = 0; i < g_gn; i++) CHECK(g_ghits[i] == i);
    /* первое пересечение в кадре играет сразу (без задержки), остальные - следом по порядку */
    CHECK(g_gfr[0] == 0.0f);
    for (int i = 1; i < g_gn; i++) CHECK(g_gfr[i] > g_gfr[i - 1] && g_gfr[i] < 1.0f);
}

/* быстрые аккорды: код с октавой и позицией на грифе; старые коды - октава 0, позиция авто */
static void test_quick(void) {
    for (int gp = 0; gp <= GT_POS_MAX; gp++)
        for (int oct = -2; oct <= 2; oct++)
            for (int mod = 0; mod < N_MODS; mod++) {
                int r, mi, md, oc, gq, c = quick_code(11, 1, mod, oct, gp);
                CHECK(c > 0 && c <= QUICK_MAX);
                quick_decode(c, &r, &mi, &md, &oc, &gq);
                CHECK(r == 11 && mi == 1 && md == mod && oc == oct && gq == gp);
            }
    int r, mi, md, oc, gq;
    quick_decode(1 + 7 + 12 * 0 + 24 * 3, &r, &mi, &md, &oc, &gq);   /* как писала v0.8 */
    CHECK(r == 7 && mi == 0 && md == 3 && oc == 0 && gq == 0);
    quick_decode(1 + 7 + 24 * 3 + 216 * 4, &r, &mi, &md, &oc, &gq);  /* как писала v0.9 (октава -1) */
    CHECK(r == 7 && md == 3 && oc == -1 && gq == 0);
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

    /* хитбоксы (как просил автор): слева, где выступают чёрные, - только чёрные (и в щелях);
     * на середине и передней части белых - только белые; между ними узкий переход по рисунку */
    float xk = (BK_X0 + WK_X0) / 2.0f, xm = WK_X0 + 20.0f, xw = (WK_X0 + WK_X1) / 2.0f, xf = (WK_XF + WK_X1) / 2.0f;
    for (float y = KB_TOP - 10.0f; y <= KB_BOTTOM + 10.0f; y += 1.0f) {
        CHECK(key_is_black(strum_key_at(xk, y)));
        CHECK(!key_is_black(strum_key_at(xw, y)));
        CHECK(!key_is_black(strum_key_at(xf, y)));
    }
    for (int i = 0; i < N_STRINGS; i++) {
        if (key_is_black(i)) {
            CHECK(strum_key_at(xk, key_center_y(i)) == i);
            CHECK(strum_key_at(xm, key_center_y(i)) == i);
            CHECK(strum_key_at(xm, key_center_y(i) + BK_H / 2.0f - 0.5f) == i);   /* у самого края чёрной */
            CHECK(strum_key_at(xm, key_center_y(i) - BK_H / 2.0f + 0.5f) == i);
        } else {
            float y0, y1, b0, b1;
            white_key_extents(i, &y0, &y1, &b0, &b1);
            CHECK(strum_key_at(xw, key_center_y(i)) == i);
            CHECK(strum_key_at(xm, (b0 + b1) / 2.0f) == i);
        }
    }
    /* проведения снизу вверх: по белым - только белые, слева - только чёрные, в переходе - все */
    {
        int keys[N_STRINGS], nw = 0, nb = 0;
        float fr[N_STRINGS];
        for (int i = 0; i < N_STRINGS; i++) { nw += !key_is_black(i); nb += key_is_black(i); }
        float xs[3] = { xw, xk, xm };
        int want[3] = { nw - 1, nb - 1, N_STRINGS - 1 };
        for (int z = 0; z < 3; z++) {
            int cur = strum_key_at(xs[z], KB_BOTTOM - 1.0f);
            int n = strum_cross(&cur, xs[z], KB_BOTTOM - 1.0f, xs[z], KB_TOP + 1.0f, keys, fr, N_STRINGS);
            for (int i = 0; i < n; i++) {
                if (z == 0) CHECK(!key_is_black(keys[i]));
                if (z == 1) CHECK(key_is_black(keys[i]));
                if (i) CHECK(keys[i] > keys[i - 1] && fr[i] >= fr[i - 1]);
            }
            if (n != want[z]) { printf("FAIL keys sweep %d: %d != %d\n", z, n, want[z]); fails++; }
        }
    }

    /* движение вбок с клавиш на нити и струны: положение меняется плавно, без скачков */
    for (float y = KB_TOP; y <= KB_BOTTOM; y += 7.0f) {
        float prev = strum_pos_from_xy(WK_X1, y);
        for (float x = WK_X1; x <= BAR_X1; x += 2.0f) {
            float p = strum_pos_from_xy(x, y);
            CHECK(fabsf(p - prev) < 0.1f);
            prev = p;
        }
    }
}

/* аппликатуры гитары: известные формы */
static void test_guitar(void) {
    struct { int root, minor, mod; const char *shape; } K[] = {
        { 0, 0, 0, "x 3 2 0 1 0" }, { 7, 0, 0, "3 2 0 0 0 3" }, { 2, 0, 0, "x x 0 2 3 2" },
        { 9, 0, 0, "x 0 2 2 2 0" }, { 4, 0, 0, "0 2 2 1 0 0" }, { 9, 1, 0, "x 0 2 2 1 0" },
        { 4, 1, 0, "0 2 2 0 0 0" }, { 2, 1, 0, "x x 0 2 3 1" }, { 5, 0, 0, "1 3 3 2 1 1" },
        { 11, 1, 0, "x 2 4 4 3 2" }, { 7, 0, 1, "3 2 0 0 0 1" }, { 9, 1, 1, "x 0 2 0 1 0" },
        { 0, 0, 2, "x 3 2 0 0 0" }, { 4, 0, 1, "0 2 0 1 0 0" },
    };
    char b[32];
    for (unsigned i = 0; i < sizeof(K) / sizeof(K[0]); i++) {
        GuitarChord g;
        guitar_voicing(&g, 1, K[i].root, chord_type(K[i].minor, K[i].mod), 0, 0);
        guitar_shape_str(&g, b, sizeof(b));
        if (strcmp(b, K[i].shape)) { printf("FAIL guitar %d: %s != %s\n", (int)i, b, K[i].shape); fails++; }
    }
    /* любой аккорд: все звуки из аккорда, тоника есть, звучит не меньше 4 струн, хватает 4 пальцев */
    for (int r = 0; r < 12; r++)
        for (int t = 0; t < N_TYPES; t++) {
            GuitarChord g;
            guitar_voicing(&g, 1, r, t, 0, 0);
            int mask = chord_mask(r, t), got = 0, snd = 0;
            for (int s = 0; s < N_GSTR; s++)
                if (g.fret[s] >= 0) { snd++; got |= 1 << (g.note[s] % 12); CHECK(mask & (1 << (g.note[s] % 12))); }
            CHECK(snd >= 4);
            CHECK(got & (1 << r));
        }
    /* позиции: известные формы выше по грифу */
    struct { int root, minor, pos; const char *shape; } P[] = {
        { 0, 0, 3, "x 3 5 5 5 3" }, { 0, 0, 8, "8 10 10 9 8 8" }, { 7, 0, 3, "3 5 5 4 3 3" },
        { 9, 1, 5, "5 7 7 5 5 5" }, { 5, 0, 1, "1 3 3 2 1 1" },
    };
    for (unsigned i = 0; i < sizeof(P) / sizeof(P[0]); i++) {
        GuitarChord gp;
        guitar_voicing(&gp, 1, P[i].root, chord_type(P[i].minor, 0), 0, P[i].pos);
        guitar_shape_str(&gp, b, sizeof(b));
        if (strcmp(b, P[i].shape)) { printf("FAIL guitar pos %d: %s != %s\n", (int)i, b, P[i].shape); fails++; }
    }
    /* в любой позиции и для любого аккорда - только звуки аккорда, лады не выше 12 */
    for (int r = 0; r < 12; r++)
        for (int t = 0; t < N_TYPES; t++)
            for (int ps = 1; ps <= GT_POS_MAX; ps++) {
                GuitarChord gp;
                guitar_voicing(&gp, 1, r, t, 0, ps);
                for (int s2 = 0; s2 < N_GSTR; s2++) {
                    CHECK(gp.fret[s2] <= 12);
                    if (gp.fret[s2] >= 0) CHECK(chord_mask(r, t) & (1 << (gp.note[s2] % 12)));
                }
            }
    /* у каждой позиции своя форма; у мажора и минора - 1..4 привычные формы с басом на 5-6 струне
     * (кроме открытых D и Dm), у остальных - 1..9, рука не ниже позиции */
    for (int r = 0; r < 12; r++)
        for (int t = 0; t < N_TYPES; t++) {
            int pl[GT_POS_MAX], np = guitar_positions(r, t, pl);
            int triad = t == chord_type(0, 0) || t == chord_type(1, 0);
            CHECK(np >= 1 && np <= (triad ? 4 : GT_POS_CX));
            GuitarChord ps[GT_POS_MAX];
            for (int i = 0; i < np; i++) {
                guitar_voicing(&ps[i], 1, r, t, 0, pl[i]);
                CHECK(ps[i].pos == pl[i]);
                int snd = 0, bass = -1;
                for (int s2 = 0; s2 < N_GSTR; s2++) {
                    if (ps[i].fret[s2] >= 0) { snd++; if (bass < 0) bass = s2; }
                    if (!triad && pl[i] >= 2) CHECK(ps[i].fret[s2] < 0 || ps[i].fret[s2] >= pl[i]);
                }
                CHECK(snd >= (triad ? 5 : 3) || (triad && r == 2));
                if (triad && r != 2) {
                    CHECK(bass <= 1);
                    CHECK(((GT_TUNING[bass] + ps[i].fret[bass]) % 12) == r);     /* в басу тоника */
                }
                for (int j = 0; j < i; j++) {
                    int same = 1;
                    for (int s2 = 0; s2 < N_GSTR; s2++) if (ps[i].fret[s2] != ps[j].fret[s2]) same = 0;
                    if (same) { printf("FAIL guitar: %d/%d pos %d == pos %d\n", r, t, pl[i], pl[j]); fails++; }
                }
            }
            /* любая позиция - к ближайшей доступной */
            for (int q = 1; q <= GT_POS_MAX; q++) {
                GuitarChord gq;
                guitar_voicing(&gq, 1, r, t, 0, q);
                if (triad) CHECK(gq.pos == guitar_snap_pos(r, t, q));
            }
        }
    {   /* си-бемоль: первая позиция - баррэ "ля" (замечание автора), вторая - баррэ "ми" */
        GuitarChord a;
        char bb[32];
        guitar_voicing(&a, 1, 10, chord_type(0, 0), 0, 1);
        guitar_shape_str(&a, bb, sizeof(bb));
        CHECK(strcmp(bb, "x 1 3 3 3 1") == 0);
        guitar_voicing(&a, 1, 10, chord_type(0, 0), 0, 0);
        guitar_shape_str(&a, bb, sizeof(bb));
        CHECK(strcmp(bb, "x 1 3 3 3 1") == 0);
        guitar_voicing(&a, 1, 10, chord_type(0, 0), 0, 6);
        guitar_shape_str(&a, bb, sizeof(bb));
        CHECK(strcmp(bb, "6 8 8 7 6 6") == 0);
    }
    GuitarChord g;
    guitar_voicing(&g, 0, 0, 0, 12, 0);                  /* без аккорда - открытые струны, со сдвигом */
    for (int s = 0; s < N_GSTR; s++) CHECK(g.fret[s] == 0 && g.note[s] == GT_TUNING[s] + 12);
}

/* тональность: лад, ступени, качество; струны по ладу; ладонь укорачивает звук */
static void test_key(void) {
    CHECK(key_scale_mask(0, 0) == 0xAB5);                         /* до мажор: C D E F G A B */
    CHECK(key_scale_mask(9, 1) == key_scale_mask(0, 0));          /* ля минор - те же звуки */
    CHECK(key_degree(0, 0, 2) == 1 && key_degree_quality(0, 1) == 1);   /* ii = Dm */
    CHECK(key_degree(0, 0, 11) == 6 && key_degree_quality(0, 6) == 2);  /* vii = Bdim */
    CHECK(key_degree(0, 0, 1) == -1);                              /* C# не из лада */
    CHECK(key_degree(9, 1, 7) == 6 && key_degree_quality(1, 6) == 0);   /* в ля миноре G - мажор (VII) */
    CHECK(strcmp(key_degree_name(0, 4), "V") == 0 && strcmp(key_degree_name(1, 0), "i") == 0);
    StringMap m;
    stringmap_build_scale(&m, key_scale_mask(7, 0), 60);          /* соль мажор от C4 */
    CHECK(m.nzones == N_STRINGS);
    for (int i = 0; i < N_STRINGS; i++) {
        CHECK(key_scale_mask(7, 0) & (1 << (m.note[i] % 12)));
        if (i) CHECK(m.note[i] > m.note[i - 1] && m.note[i] - m.note[i - 1] <= 2);
    }
    CHECK(m.note[0] == 60 && m.note[24] - m.note[0] >= 40);       /* больше трёх октав */

    /* ладонь: та же струна гаснет заметно быстрее */
    int16_t b[256];
    long e_open = 0, e_palm = 0;
    synth_init();
    synth_guitar(2, 55, 5, 0, 0.8f);
    for (int i = 0; i < 80; i++) synth_render(b, 256);
    for (int i = 0; i < 40; i++) { synth_render(b, 256); for (int k = 0; k < 256; k++) e_open += abs(b[k]); }
    synth_init();
    synth_palm(1);
    synth_guitar(2, 55, 5, 0, 0.8f);
    for (int i = 0; i < 80; i++) synth_render(b, 256);
    for (int i = 0; i < 40; i++) { synth_render(b, 256); for (int k = 0; k < 256; k++) e_palm += abs(b[k]); }
    synth_palm(0);
    printf("palm: level after 0.4 s %ld vs open %ld\n", e_palm, e_open);

    /* лад: та же нота E4 открытой первой струной звонче, чем на 9-м ладу третьей */
    double hf[2];
    for (int pass = 0; pass < 2; pass++) {
        synth_init();
        if (pass == 0) synth_guitar(5, 64, 0, 0, 0.8f); else synth_guitar(3, 64, 9, 0, 0.8f);
        double e = 0, d = 0;
        int prev = 0;
        for (int i = 0; i < 40; i++) {
            synth_render(b, 256);
            for (int k = 0; k < 256; k++) { e += (double)b[k] * b[k]; d += (double)(b[k] - prev) * (b[k] - prev); prev = b[k]; }
        }
        hf[pass] = d / (e + 1.0);
    }
    printf("fret timbre: open %.4f vs 9th fret %.4f (доля верхов)\n", hf[0], hf[1]);
    CHECK(hf[1] < hf[0] * 0.7);
    CHECK(e_palm * 10 < e_open);

    /* сустейн: нота страммера через 2 с звучит заметно громче, но через 12 с почти стихла */
    long e_dry = 0, e_sus = 0, e_tail = 0;
    for (int pass = 0; pass < 2; pass++) {
        synth_init();
        synth_sustain(pass);
        synth_strum(60, 0);
        long e = 0;
        for (int i = 0; i < 375; i++) synth_render(b, 256);                     /* 2 с */
        for (int i = 0; i < 20; i++) { synth_render(b, 256); for (int k = 0; k < 256; k++) e += abs(b[k]); }
        if (pass) {
            e_sus = e;
            for (int i = 0; i < 1875; i++) synth_render(b, 256);                /* ещё 10 с */
            for (int i = 0; i < 20; i++) { synth_render(b, 256); for (int k = 0; k < 256; k++) e_tail += abs(b[k]); }
        } else e_dry = e;
    }
    synth_sustain(0);
    printf("sustain: level after 2 s %ld vs dry %ld, after 12 s %ld\n", e_sus, e_dry, e_tail);
    CHECK(e_sus > e_dry * 4);
    CHECK(e_tail * 40 < e_sus);                     /* гаснет: не бесконечно */
}

/* dreamy-звук струн: звучит, не клиппует, гаснет; цена против классики - в выводе */
static void wav_write(const char *path, const int16_t *s, int n);
static void test_dreamy(void) {
    static int16_t buf[48000 * 14];
    const int N = 48000 * 14;
    double t_cls = 0, t_drm = 0;
    double t_anc = 0;
    for (int pass = 0; pass < 3; pass++) {
        synth_init();
        synth_strum_sound(pass);
        clock_t c0 = clock();
        int clip = 0, pk1 = 0, pk_end = 0;
        for (int pos = 0; pos < N; pos += 256) {
            if (pos == 0) for (int k = 0; k < 24; k++) synth_strum(48 + k * 2, k * 300);   /* 24 голоса */
            synth_render(buf + pos, 256);
            for (int i = 0; i < 256; i++) {
                int a = abs(buf[pos + i]);
                if (a >= 32000) clip++;
                if (pos < 48000 && a > pk1) pk1 = a;
                if (pos > N - 48000 && a > pk_end) pk_end = a;
            }
        }
        double t = (double)(clock() - c0) / CLOCKS_PER_SEC;
        if (pass == 2) t_anc = t; else if (pass) t_drm = t; else t_cls = t;
        static const char *NM[3] = { "classic", "dreamy", "ancient" };
        printf("%s: peak %d, clip %d, tail peak %d, %.1f ms per audio second\n", NM[pass], pk1, clip, pk_end, t * 1000.0 / 14.0);
        CHECK(clip == 0 && pk1 > 3000);
        if (pass) CHECK(pk_end * 20 < pk1);
        if (pass == 1) wav_write("out/dreamy_demo.wav", buf, N);
        if (pass == 2) wav_write("out/ancient_demo.wav", buf, N);
    }
    printf("cpu vs classic (24 voices): dreamy x%.2f, ancient x%.2f\n", t_drm / (t_cls > 1e-9 ? t_cls : 1e-9), t_anc / (t_cls > 1e-9 ? t_cls : 1e-9));
    synth_strum_sound(0);
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

/* звук гитары: удар вниз по G, потом одна и та же нота E4 на трёх струнах - открытая тонкая,
 * B-струна 5 лад, G-струна 9 лад (должны звучать по-разному), в конце удар по заглушённым струнам.
 * Пишет out/guitar_demo.wav; проверка: нет перегруза, всё затухает */
static void test_guitar_sound(void) {
    const int G = 256, total = SYNTH_RATE * 9;
    int16_t *out = calloc((size_t)total, 2);
    synth_init();
    GuitarChord gc;
    guitar_voicing(&gc, 1, 7, chord_type(0, 0), 0, 0);
    int pos = 0, clip = 0;
    for (int blk = 0; pos + G <= total; blk++, pos += G) {
        if (blk == 5)  for (int s = 0; s < N_GSTR; s++) synth_guitar(s, gc.note[s], gc.fret[s], s * 500, 0.9f);
        if (blk == 300) synth_guitar(5, 64, 0, 0, 0.8f);
        if (blk == 420) synth_guitar(4, 64, 5, 0, 0.8f);
        if (blk == 540) synth_guitar(3, 64, 9, 0, 0.8f);
        if (blk == 700) { for (int s = 0; s < N_GSTR; s++) synth_guitar(s, -(GT_TUNING[s]), 0, s * 300, 0.7f); }
        synth_render(out + pos, G);
        for (int i = 0; i < G; i++) if (out[pos + i] >= 32000 || out[pos + i] <= -32000) clip++;
    }
    CHECK(clip == 0);
    CHECK(synth_guitar_active() == 0);
    printf("guitar: clipped %d\n", clip);
    wav_write("out/guitar_demo.wav", out, pos);
    free(out);
}

int main(void) {
    test_theory();
    test_strum();
    test_keys();
    test_quick();
    test_gtr_tremolo();
    test_guitar();
    test_key();
    test_guitar_sound();
    test_synth();
    test_dreamy();
    printf(fails ? "\n%d FAILED\n" : "\nall tests passed\n", fails);
    return fails ? 1 : 0;
}
