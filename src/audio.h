/* Аудиопоток Vita: открывает порт, забирает события из очереди, рендерит синт.
 * Очередь - кольцо "один пишет (интерфейс), один читает (аудио)", без блокировок. */
#ifndef AUDIO_H
#define AUDIO_H

#define AUDIO_GRAIN 128      /* 2.7 мс при 48 кГц: меньше задержка от касания до звука */

int audio_start(void);
void audio_stop(void);
void audio_reset_port(void);              /* после "сна" консоли */

void audio_strum(int note, int delay_samples);
void audio_pad(const int *notes, int n);
/* гитара: щипок струны (muted - заглушённая, note - нота открытой струны), сила 0..1 */
void audio_guitar(int string, int note, int fret, int muted, float vel, int delay_samples);   /* fret - лад, для тембра */
void audio_guitar_damp(int mask);                 /* заглушить струны по маске */
void audio_volume(int strings, int pad);          /* 0..100 */
void audio_sustain(int on);                        /* сустейн (задняя панель, левая рука) */
void audio_palm(int on);                          /* ладонь на струнах (задняя панель) */
void audio_wide(int on);                          /* объёмный звук: правый канал +10 мс */

/* диагностика для верхней полосы */
int audio_xruns(void);                    /* сколько раз блок пришёл заметно позже */
int audio_voices(void);
int audio_peak(void);
int audio_error(void);

#endif
