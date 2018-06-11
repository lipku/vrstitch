#ifndef AUDIO_CAP_H
#define AUDIO_CAP_H

#include "portaudio.h"  


class app;
class AudioCap
{
public:
	AudioCap(int index, app *pApp);
	~AudioCap();

	int StartCap();
	int StopCap();

	int waveCaptureProcess(const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags);

private:
	int				m_index;
	app             *m_pApp;

	PaStream *stream;

};


#endif