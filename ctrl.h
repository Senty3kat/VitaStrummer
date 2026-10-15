#ifndef STUB_AUDIOOUT_H
#define STUB_AUDIOOUT_H
typedef enum { SCE_AUDIO_OUT_PORT_TYPE_MAIN = 0, SCE_AUDIO_OUT_PORT_TYPE_BGM = 1, SCE_AUDIO_OUT_PORT_TYPE_VOICE = 2 } SceAudioOutPortType;
typedef enum { SCE_AUDIO_OUT_MODE_MONO = 0, SCE_AUDIO_OUT_MODE_STEREO = 1 } SceAudioOutMode;
int sceAudioOutOpenPort(SceAudioOutPortType type, int len, int freq, SceAudioOutMode mode);
int sceAudioOutReleasePort(int port);
int sceAudioOutOutput(int port, const void *buf);
#endif
