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
/* следующий блок, моно */
void synth_render(int16_t *out, int n);

int synth_active_voices(void);
int synth_peak(void);          /* пик последнего блока, 0..32767 */

#endif
