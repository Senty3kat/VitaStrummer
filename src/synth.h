/* Простой синт для отладки: треугольная волна, два вида голосов.
 *   STRUM - быстрая атака и затухание (щипок струны)
 *   PAD   - звучит, пока аккорд выбран, мягкая атака и затухание
 * Вызывается только из аудиопотока, блокировок нет. */
#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

#define SYNTH_RATE 48000
#define SYNTH_VOICES 32

void synth_init(void);
/* нота струны; delay - через сколько сэмплов от начала следующего блока */
void synth_strum(int note, int delay);
/* заменить звучащий аккорд пэда (n = 0 - тишина) */
void synth_pad(const int *notes, int n);
void synth_all_off(void);
/* громкость слоёв 0..1: струны (страммер и гитара) и пэд */
void synth_volume(float strings, float pad);
/* ладонь на струнах (задняя панель): звенящее гаснет, новые щипки короткие и глухие */
void synth_palm(int on);
void synth_sustain(int on);   /* сустейн: ноты звучат дольше (но не бесконечно) */

/* гитара: щипок струны s (0 - самая толстая) нотой note; note < 0 - заглушенная струна
 * (-нота открытой струны: короткий глухой стук). vel 0..1 - сила удара */
#define N_GSTR_SYNTH 6
void synth_guitar(int s, int note, int delay, float vel);
void synth_guitar_damp(int mask);       /* заглушить струны по маске битов */
void synth_guitar_damp_tau(int mask, float tau);
int  synth_guitar_active(void);
/* следующий блок, моно */
void synth_render(int16_t *out, int n);

int synth_active_voices(void);
int synth_peak(void);          /* пик последнего блока, 0..32767 */

#endif
