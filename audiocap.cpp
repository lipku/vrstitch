
#include "audiocap.h"
#include "app.h"
#include <stdio.h>

#define SAMPLE_RATE  (44100)
#define FRAMES_PER_BUFFER (1024)
#define NUM_CHANNELS    (2)

#define PA_SAMPLE_TYPE  paInt16
typedef short SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"


static int recordCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
 {
 	AudioCap *pCap = (AudioCap *)userData;
	pCap->waveCaptureProcess(inputBuffer,outputBuffer,framesPerBuffer,timeInfo,statusFlags);

	return 0;
 }

int AudioCap::waveCaptureProcess(const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags)
{
	 /* Write recorded data to a file. */  
    //fwrite( inputBuffer, NUM_CHANNELS * sizeof(SAMPLE), framesPerBuffer, fid ); 
    m_pApp->encodeAAC(inputBuffer, NUM_CHANNELS*sizeof(SAMPLE)*framesPerBuffer, (int64_t)timeInfo->currentTime);
}

AudioCap::AudioCap(int index, app *pApp)
{
	m_index = index;
	m_pApp = pApp;

	PaError err = Pa_Initialize();
}


AudioCap::~AudioCap()
{
	StopCap();
	Pa_Terminate(); 
}

int AudioCap::StartCap()
{

	PaStreamParameters  inputParameters;
	inputParameters.device = Pa_GetDefaultInputDevice(); /* default input device */
    if (inputParameters.device == paNoDevice) {
        fprintf(stderr,"Error: No default input device.\n");
        return -1;
    }
    inputParameters.channelCount = 2;                    /* stereo input */
    inputParameters.sampleFormat = PA_SAMPLE_TYPE;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    /* Record some audio. -------------------------------------------- */
    PaError err = Pa_OpenStream(
              &stream,
              &inputParameters,
              NULL,                  /* &outputParameters, */
              SAMPLE_RATE,
              FRAMES_PER_BUFFER,
              paClipOff,      /* we won't output out of range samples so don't bother clipping them */
              recordCallback,
              this );
    if( err != paNoError ) 
    	return -1;

    err = Pa_StartStream( stream );

	return err;
}

int AudioCap::StopCap()
{
	PaError err = Pa_CloseStream( stream ); 
	return err;
}