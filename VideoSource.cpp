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

#include "VideoSource.h"
#include "app.h"
//#include "FrameQueue.h"

#include <time.h>
#include <assert.h>
#include "helper_cuda_drvapi.h"

#include "FrameQueue.h"
#include "VideoParser.h"

#include "g711.h"

static unsigned int msecond()
{
	timeval tv;
	gettimeofday(&tv,NULL);
	return tv.tv_sec*1000 + tv.tv_usec/1000;
}


VideoSource::VideoSource()
{
	bThreadExit = false;
	bStarted = false;
	play_thread_ptr = 0;

	recordPath_ = "";
	startMS = msecond();

	m_aacEncHandle = NULL;
	flvHandle_ = NULL;

}

VideoSource::~VideoSource()
{
	stop();
	CloseRtspClient((RTSPClient * )m_hRtspHandle);

	if (src_data_)
		av_freep(&src_data_[0]);
	av_freep(&src_data_);

	if (dst_data_)
		av_freep(&dst_data_[0]);
	av_freep(&dst_data_);

	swr_free(&swr_ctx_);

	//if(mp4fileHandle_)
	//	MP4Close(mp4fileHandle_);
	if (flvHandle_)
		flv_write_trailer(flvHandle_);
}


void funcStreamCallback(unsigned char *pBuffer, unsigned int dwBufSize,unsigned int timestamp,
									  unsigned int marker,const char* payloadtype,void* pContext)
{
	VideoSource *pRtspDemux=(VideoSource *)pContext;
	pRtspDemux->ProcessStream(pBuffer,dwBufSize,timestamp,marker,payloadtype);
}

void VideoSource::setParser(VideoParser &rVideoParser, CUcontext cuCtx, FrameQueue *pFrameQueue)
{
	oSourceData_.hVideoParser = rVideoParser.hParser_;
	oSourceData_.pFrameQueue = pFrameQueue;
	oContext_ = cuCtx;
}


int VideoSource::ProcessStream(unsigned char *pBuffer, unsigned int dwBufSize,unsigned int timestamp,
									  unsigned int marker,const char* payloadtype)
{
	fRecvMediaNum++;

	if (!strcmp(payloadtype, "H264")) {
		//m_pApp->feedVideoData(m_index, pBuffer, dwBufSize, timestamp);
		cuCtxPushCurrent(oContext_);
		CUVIDSOURCEDATAPACKET cupkt;
		cupkt.payload_size = (unsigned long)dwBufSize;
		cupkt.payload = (const unsigned char*)pBuffer;
		cupkt.flags = CUVID_PKT_TIMESTAMP;
		cupkt.timestamp = timestamp;

		CUresult oResult = cuvidParseVideoData(oSourceData_.hVideoParser, &cupkt);
		checkCudaErrors(cuCtxPopCurrent(NULL));

		//int mark = 0xffffffff; //to del
		//fwrite(&mark, 1, 4, fp_test); //to del
		//fwrite(pBuffer, 1, dwBufSize, fp_test); //to del
		//fprintf(fp_test, "%d %x %d\n", timestamp, pBuffer[4], dwBufSize);

		/*if (mp4VideoTrack_)
		{
			MP4WriteSample(mp4fileHandle_, mp4VideoTrack_, (const uint8_t *)pBuffer, dwBufSize, MP4_INVALID_DURATION, 0, 1);
		}*/
		if (flvHandle_)
		{
			int iskeyframe = ((uint8_t *)pBuffer)[4] == 0x67 || ((uint8_t *)pBuffer)[4] == 0x65;
			flv_write_video_packet(flvHandle_, iskeyframe, (uint8_t *)pBuffer, dwBufSize, msecond() - startMS); /// 10000
		}
	}
	else if (!strcmp(payloadtype, "JPEG")) {
		m_pApp->feedVideoData(m_index, pBuffer, dwBufSize, timestamp);

		//fwrite(pBuffer, 1, dwBufSize, fp_test); //to del
	}
	else if (!strcmp(payloadtype, "PCMA")) {

		short *pcmbuf = (short*)src_data_[0];
		for (int i = 0; i < dwBufSize; i++)
		{
			pcmbuf[i] = Snack_Alaw2Lin(pBuffer[i]);
		}

		/* convert to destination format */
		int ret = swr_convert(swr_ctx_, dst_data_, dst_nb_samples_, (const uint8_t **)src_data_, dwBufSize);
		if (ret < 0) {
			fprintf(stderr, "Error while converting\n");
			return -1;
		}
		int dst_linesize, dst_nb_channels = 2;
		enum AVSampleFormat dst_sample_fmt = AV_SAMPLE_FMT_S16; //AV_SAMPLE_FMT_FLT;
		int dst_bufsize = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels,
			ret, dst_sample_fmt, 1);
		if (dst_bufsize < 0) {
			fprintf(stderr, "Could not get sample buffer size\n");
			return -1;
		}

		//m_pApp->feedAudioData(m_index, dst_data_[0], dst_bufsize, timestamp);
		int outsize = encodeAAC((void *)dst_data_[0], dst_bufsize, timestamp);
		if(outsize>0)
		{
			if (flvHandle_)
			{
				flv_write_audio_packet(flvHandle_, (uint8_t *)m_aacOutbuf+7, outsize-7, msecond() - startMS); //timestamp
			}
			if(m_index==3)
				m_pApp->onAudioStitchedOutput(m_aacOutbuf, outsize, timestamp);
		}

	}
	/*else {
		if(m_audioflag)
			m_pApp->feedAudioData(m_index, pBuffer, dwBufSize, timestamp);
	}*/
	
	return 0;
}

void TaskMonitorDataCallback(void* clientData) {
	VideoSource* pSource = (VideoSource*)clientData;
	pSource->TaskMonitorData();
}

void VideoSource::TaskMonitorData() {
	if (fRecvMediaNum <= 0)
	{
		printf("Failed to recv media data in 10s for URL %s\n", rtspUrl_.c_str());
		CloseRtspClient((RTSPClient *)m_hRtspHandle);

		m_hRtspHandle = openURL(*env, rtspUrl_.c_str(), -1, funcStreamCallback, this);
	}

	fRecvMediaNum = 0;

	env->taskScheduler().scheduleDelayedTask(10000 * 1000, TaskMonitorDataCallback, this); //10 second
}


bool VideoSource::init(const std::string sFileName, int index, app *pApp, bool audioflag)
{
	//assert(0 != pFrameQueue);

	int				i;

	m_index = index;
	m_pApp = pApp;
	m_audioflag = audioflag;
	
	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	env = BasicUsageEnvironment::createNew(*scheduler);

	rtspUrl_ = sFileName;
	fRecvMediaNum = 0;
	
	m_hRtspHandle = openURL(*env, sFileName.c_str(), -1, funcStreamCallback,this);

	env->taskScheduler().scheduleDelayedTask(10000 * 1000, TaskMonitorDataCallback, this); //10 second

	if(audioflag)
		initAACEncode(2, 44100, 64000);

	return true;
}

int VideoSource::setRecordPath(const std::string record_path)
{
	recordPath_ = record_path;
	return 0;
}

void *playProc(void* lpParam)
{
	VideoSource *pSource = (VideoSource *)lpParam;
	pSource->play_thread();
}

void VideoSource::start()
{
	bThreadExit = TRUE;
	if (play_thread_ptr)
	{
		pthread_join(play_thread_ptr, NULL);
		play_thread_ptr = 0;
	}

	/*************mp4file******************************/
	if (!recordPath_.empty())
	{
		char filename[255];
		//sprintf(filename, "%s/cam%d.hisi264", recordPath_.c_str(), m_index); //to del
		//fp_test = fopen(filename, "wb"); //to del

		time_t rawtime;
		struct tm * timeinfo;
		char timestr[100];
		time(&rawtime);
		timeinfo = localtime(&rawtime);
		strftime(timestr, sizeof(timestr), "%Y%m%d-%H%M%S", timeinfo);

		sprintf(filename, "%s/cam%d_%s.flv", recordPath_.c_str(), m_index, timestr);
		flvHandle_ = flv_init(filename, 30, 2048, 1536);
		/*mp4fileHandle_ = MP4Create(filename);//创建mp4文件
		if (mp4fileHandle_ == MP4_INVALID_FILE_HANDLE)
		{
			printf("open file fialed.\n");
		}

		//MP4SetTimeScale(mp4fileHandle_, 90000);

		//添加h264 track    
		mp4VideoTrack_ = MP4AddH264VideoTrack(mp4fileHandle_, 90000, 90000 / 25, 1920, 1080,
			0x64,//0x64, //sps[1] AVCProfileIndication
			0x00, //sps[2] profile_compat
			0x1e,//0x1f, //sps[3] AVCLevelIndication
			3); // 4 bytes length before each NAL unit
		if (mp4VideoTrack_ == MP4_INVALID_TRACK_ID)
		{
			printf("add video track failed.\n");
		}
		//MP4SetVideoProfileLevel(mp4fileHandle_, 0x7F);

		//添加aac音频

		mp4AudioTrack_ = MP4AddAudioTrack(mp4fileHandle_, 44100, 1024, MP4_MPEG4_AUDIO_TYPE);
		if (mp4AudioTrack_ == MP4_INVALID_TRACK_ID)
		{
			printf("add audio track failed.\n");
		}
		MP4SetAudioProfileLevel(mp4fileHandle_, 0x2);*/
	}
	/***********resample********************************************/
	int64_t src_ch_layout = AV_CH_LAYOUT_MONO, dst_ch_layout = AV_CH_LAYOUT_STEREO;//AV_CH_LAYOUT_MONO;
	int src_rate = 8000, dst_rate = 44100;
	enum AVSampleFormat src_sample_fmt = AV_SAMPLE_FMT_S16, dst_sample_fmt = AV_SAMPLE_FMT_S16; //AV_SAMPLE_FMT_FLT;
	int ret;
	int src_nb_channels = 0, dst_nb_channels = 0;
	int src_linesize, dst_linesize;
	src_nb_samples_ = 1024;

	/* create resampler context */
	swr_ctx_ = swr_alloc();
	if (!swr_ctx_) {
		fprintf(stderr, "Could not allocate resampler context\n");
		ret = AVERROR(ENOMEM);
		return;
	}

	/* set options */
	av_opt_set_int(swr_ctx_, "in_channel_layout", src_ch_layout, 0);
	av_opt_set_int(swr_ctx_, "in_sample_rate", src_rate, 0);
	av_opt_set_sample_fmt(swr_ctx_, "in_sample_fmt", src_sample_fmt, 0);

	av_opt_set_int(swr_ctx_, "out_channel_layout", dst_ch_layout, 0);
	av_opt_set_int(swr_ctx_, "out_sample_rate", dst_rate, 0);
	av_opt_set_sample_fmt(swr_ctx_, "out_sample_fmt", dst_sample_fmt, 0);

	/* initialize the resampling context */
	if ((ret = swr_init(swr_ctx_)) < 0) {
		fprintf(stderr, "Failed to initialize the resampling context\n");
		return;
	}

	/* allocate source and destination samples buffers */
	src_nb_channels = av_get_channel_layout_nb_channels(src_ch_layout);
	ret = av_samples_alloc_array_and_samples(&src_data_, &src_linesize, src_nb_channels,
		src_nb_samples_, src_sample_fmt, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate source samples\n");
		return ;
	}

	/* compute the number of converted samples: buffering is avoided
	* ensuring that the output buffer will contain at least all the
	* converted input samples */
	max_dst_nb_samples_ = dst_nb_samples_ =
		av_rescale_rnd(src_nb_samples_, dst_rate, src_rate, AV_ROUND_UP);

	/* buffer is going to be directly written to a rawaudio file, no alignment */
	dst_nb_channels = av_get_channel_layout_nb_channels(dst_ch_layout);
	ret = av_samples_alloc_array_and_samples(&dst_data_, &dst_linesize, dst_nb_channels,
		dst_nb_samples_, dst_sample_fmt, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate destination samples\n");
		return;
	}
	/****************************************************************/

	bThreadExit = FALSE;
	pthread_create(&play_thread_ptr, NULL, playProc, (void*)this);
	if (!play_thread_ptr)
	{
		return ;
	}

	
}

void VideoSource::stop()
{
	bThreadExit = TRUE;
	watchEvent = 0xFF;
	if (play_thread_ptr)
	{
		pthread_join(play_thread_ptr, NULL);
		play_thread_ptr = 0;
	}
}

bool VideoSource::isStarted()
{
	return bStarted;
}


void VideoSource::play_thread()
{
	watchEvent = 0;
	env->taskScheduler().doEventLoop(&watchEvent); // does not return
	//oSourceData_.pFrameQueue->endDecode();
	bStarted = false;
}

//-------------------------------audio aac-------------------------------------------------------------------------
int VideoSource::initAACEncode(int channel, int samplerate, int bitrate)
{
	if (aacEncOpen(&m_aacEncHandle, 0, channel) != AACENC_OK) {
		printf("Unable to open fdkaac encoder\n");
		return -1;
	}

	if (aacEncoder_SetParam(m_aacEncHandle, AACENC_AOT, 2) != AACENC_OK) {  //aac lc
		printf("Unable to set the AOT\n");
		return -1;
	}

	if (aacEncoder_SetParam(m_aacEncHandle, AACENC_SAMPLERATE, samplerate) != AACENC_OK) {
		printf("Unable to set the AOT\n");
		return -1;
	}
	if (aacEncoder_SetParam(m_aacEncHandle, AACENC_CHANNELMODE, MODE_2) != AACENC_OK) {  //2 channle
		printf("Unable to set the channel mode\n");
		return -1;
	}
	if (aacEncoder_SetParam(m_aacEncHandle, AACENC_BITRATE, bitrate) != AACENC_OK) {
		printf("Unable to set the bitrate\n");
		return -1;
	}
	if (aacEncoder_SetParam(m_aacEncHandle, AACENC_TRANSMUX, 2) != AACENC_OK) { //0-raw 2-adts
		printf("Unable to set the ADTS transmux\n");
		return -1;
	}

	if (aacEncEncode(m_aacEncHandle, NULL, NULL, NULL, NULL) != AACENC_OK) {
		printf("Unable to initialize the encoder\n");
		return -1;
	}

	AACENC_InfoStruct info = { 0 };
	if (aacEncInfo(m_aacEncHandle, &info) != AACENC_OK) {
		printf("Unable to get the encoder info\n");
		return -1;
	}
	return 0;
}

int VideoSource::encodeAAC(const void* pData, int size, int64_t timestamp)
{
	if (!m_aacEncHandle)
		return -1;

	/*EnterCriticalSection(&aCriticalSection_);
	m_params->audio_payloads[index].payload.buffer.ptr = pData;
	m_params->audio_payloads[index].payload.buffer.size = size;
	m_params->audio_payloads[index].payload.buffer.timestamp = timestamp;

	//RETURN_NVSTITCH_ERROR(nvstitchFeedStitcherAudio(0, stitcher, index, &m_params->audio_payloads[index]));

	LeaveCriticalSection(&aCriticalSection_);*/

	AACENC_BufDesc in_buf = { 0 }, out_buf = { 0 };
	AACENC_InArgs in_args = { 0 };
	AACENC_OutArgs out_args = { 0 };
	int in_identifier = IN_AUDIO_DATA;
	int in_elem_size = 2;

	in_args.numInSamples = size / 2;
	in_buf.numBufs = 1;
	in_buf.bufs = (void **)&pData;
	in_buf.bufferIdentifiers = &in_identifier;
	in_buf.bufSizes = &size;
	in_buf.bufElSizes = &in_elem_size;

	int out_identifier = OUT_BITSTREAM_DATA;
	void *out_ptr = m_aacOutbuf;
	int out_size = sizeof(m_aacOutbuf);
	int out_elem_size = 1;
	out_buf.numBufs = 1;
	out_buf.bufs = &out_ptr;
	out_buf.bufferIdentifiers = &out_identifier;
	out_buf.bufSizes = &out_size;
	out_buf.bufElSizes = &out_elem_size;

	if ((aacEncEncode(m_aacEncHandle, &in_buf, &out_buf, &in_args, &out_args)) != AACENC_OK) {
		fprintf(stderr, "Encoding aac failed\n");
		return -1;
	}
	if (out_args.numOutBytes == 0)
		return -1;
	//fwrite(outbuf, 1, out_args.numOutBytes, out);
	//onAudioStitchedOutput(m_aacOutbuf, out_args.numOutBytes, timestamp);

	return out_args.numOutBytes;
}


