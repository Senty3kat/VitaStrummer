/* Аудиопоток Vita: открывает порт, забирает события из очереди, рендерит синт.
 * Очередь - кольцо "один пишет (интерфейс), один читает (аудио)", без блокировок. */
#ifndef AUDIO_H
#define AUDIO_H

/* размер блока: 256 сэмплов = 5.3 мс при 48 кГц (в сэмплере было 1024 = 21 мс) */
#define AUDIO_GRAIN 256

int audio_start(void);
void audio_stop(void);
void audio_reset_port(void);              /* после "сна" консоли */

void audio_strum(int note, int delay_samples);
void audio_pad(const int *notes, int n);
/* гитара: щипок струны (muted - заглушённая, note - нота открытой струны), сила 0..1 */
void audio_guitar(int string, int note, int muted, float vel, int delay_samples);
void audio_guitar_damp(int mask);                 /* заглушить струны по маске */
void audio_volume(int strings, int pad);          /* 0..100 */

/* диагностика для верхней полосы */
int audio_xruns(void);                    /* сколько раз блок пришёл заметно позже */
int audio_voices(void);
int audio_peak(void);
int audio_error(void);

#endif
