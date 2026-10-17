/* MIDI по USB: Vita - USB-MIDI устройство для компьютера (плагин psvita_usb_audio_midi.skprx
 * из VitaMIDI, клиент psvita_usb_audio_midi_client.suprx лежит в VPK).
 * Передача - ноты страммера уходят на компьютер; приём - ноты с компьютера играют на Vita. */
#ifndef MIDI_H
#define MIDI_H

enum { MIDI_OFF = 0, MIDI_NO_PLUGIN, MIDI_ERROR, MIDI_WAIT_CABLE, MIDI_CONNECTED };

/* включить/выключить приём и передачу; USB захватывается, пока включено хоть что-то */
void midi_set(int rx, int tx);
int  midi_state(void);                   /* MIDI_* для страницы настроек */
int  midi_error(void);                   /* последний код ошибки (для отладки) */

/* передача: нота звучит dur секунд, потом note off (повтор той же ноты - off сразу) */
void midi_note(int note, int vel, float dur);
/* раз в кадр: отправить накопленное, погасить истёкшие ноты (dt - секунды кадра) */
void midi_frame(float dt);
/* приём: принятые note on (note, vel), до max штук; note off не нужны (страммер - щипки) */
int  midi_read(int *notes, int *vels, int max);
/* выход из приложения / «сон»: погасить ноты, отпустить USB */
void midi_shutdown(void);

#endif
