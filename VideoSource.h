/*
 * Copyright 1993-2015 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */

#ifndef NV_VIDEO_SOURCE
#define NV_VIDEO_SOURCE


#include <string>
#include <pthread.h>

#include "RTSPProtocol.h"
//#include "mp4v2\mp4v2.h"
#include "dynlink_nvcuvid.h" // <nvcuvid.h>

#include "flv.h"
#include "fdk-aac/aacenc_lib.h"
#include "fdk-aac/aacdecoder_lib.h"

//#include "thread.hpp"
extern "C"
{
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

// forward declarations
class FrameQueue;
class VideoParser;
class app;


// A wrapper class around the CUvideosource entity and API.
//  The CUvideosource manages video-source streams (right now
// via openening a file containing the stream.) After successfully
// opening a video stream, one can query its properties, such as
// video and audio compression format, frame-rate, etc.
//
// The video-source spawns its own thread for processing the stream.
// The user can register call-back methods for handling chucks of demuxed
// audio and video data.
class VideoSource
{
    public:
		VideoSource();

		~VideoSource();

		//CUVIDEOFORMAT format();

		void setParser(VideoParser &rVideoParser, CUcontext cuCtx, FrameQueue *pFrameQueue);

        // Begin processing the video stream.
        void start();

        // End processing the video stream.
        void stop();

        // Has video-processing be started?
        bool isStarted();

		bool init(const std::string sFileName, int index, app *pApp, bool audioflag);

		int setRecordPath(const std::string record_path);
			
		int ProcessStream(unsigned char *pBuffer, unsigned int dwBufSize,unsigned int timestamp,
									  unsigned int marker,const char* payloadtype);
		void TaskMonitorData();

		void play_thread();

		int encodeAAC(const void* pData, int size, int64_t timestamp);

    private:
        
        int initAACEncode(int channel, int samplerate, int bitrate);

		pthread_t play_thread_ptr;
		int bThreadExit;

		bool bStarted;

		int				m_index;
		app             *m_pApp;
		bool            m_audioflag;
		
		UsageEnvironment* env;
		void *m_hRtspHandle;
		char watchEvent;
		unsigned int fRecvMediaNum;
		std::string rtspUrl_;

		std::string recordPath_;
		//MP4FileHandle mp4fileHandle_;
		//MP4TrackId mp4VideoTrack_;
		//MP4TrackId mp4AudioTrack_;
		flv_t *flvHandle_ = NULL;
		unsigned int startMS;

		struct VideoSourceData
		{
			CUvideoparser hVideoParser;
			FrameQueue   *pFrameQueue;
		};
		VideoSourceData oSourceData_;       // Instance of the user-data struct we use in the video-data handle callback.
		//CUVIDEOFORMAT	stFormat_;
		CUcontext       oContext_;

		SwrContext *swr_ctx_ = NULL;
		uint8_t **src_data_ = NULL, **dst_data_ = NULL;
		int src_nb_samples_, dst_nb_samples_, max_dst_nb_samples_;

		HANDLE_AACENCODER   m_aacEncHandle;
		uint8_t m_aacOutbuf[8192];
		//FILE *fp_test;

};

#endif // NV_VIDEO_SOURCE

