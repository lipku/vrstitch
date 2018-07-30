

#pragma once

#include <stdint.h>
#include <vector>
#include <pthread.h>

/*
extern "C"
{
#include "libavutil/avstring.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"

#include "libavfilter/avfiltergraph.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libavutil/avutil.h"

}

*/

#include "FrameQueue.h"
#include "VideoSource.h"
#include "VideoParser.h"
#include "VideoDecoder.h"
#include "dynlink_nvcuvid.h" // <nvcuvid.h>
#include "nvEncodeAPI.h"
#include "nvCPUOPSys.h"
#include "NvHWEncoder.h"

// High Level API 
#include "nvstitch.h"
#include "nvss_video.h"
#include "nvsf.h"
//#include "mp4v2\mp4v2.h"
#include "flv.h"
#include "fdk-aac/aacenc_lib.h"
#include "fdk-aac/aacdecoder_lib.h"

typedef pthread_mutex_t CRITICAL_SECTION;


#define RETURN_NVSTITCH_ERROR(err) \
do { \
    nvstitchResult pRes = (err); \
    if (NVSTITCH_SUCCESS != pRes) \
    { \
        std::cerr << "Error at line " << __LINE__ << ": " << \
            nvstitchGetErrorString(err) << std::endl; \
        return pRes; \
    } \
} while (false)

#define RETURN_NVSS_ERROR(err) \
do { \
    nvstitchResult pRes = (err); \
    if (NVSTITCH_SUCCESS != pRes) \
    { \
        std::cerr << "Error at line " << __LINE__ << ": " << \
            nvssVideoGetErrorString(err) << std::endl; \
        return pRes; \
    } \
} while (false)

#define CHECK_NVSS_ERROR(err) \
do { \
    nvstitchResult pRes = (err); \
    if (NVSTITCH_SUCCESS != pRes) \
    { \
        std::cerr << "Error at line " << __LINE__ << ": " << \
            nvssVideoGetErrorString(err) << std::endl; \
    } \
} while (false)

#define NVSF_CALL(X) { nvstitchResult  t = (X); if(t!=NVSTITCH_SUCCESS){printf("Error at line %d\n", __LINE__); exit(-1);}}
#define NVSF_CHECK(X) { nvstitchResult  t = (X); if(t!=NVSTITCH_SUCCESS){printf("Error at line %d\n", __LINE__); }}

typedef struct _appParams {
	uint32_t pano_width;
	uint32_t num_frames;
	std::vector<nvstitchCameraProperties_t> cam_properties;
	nvstitchVideoRigProperties_t rig_properties;
	nvstitchStitcherProperties_t stitcher_properties;
	std::vector<nvstitchPayload_t> payloads;
	std::string input_dir_base;
	bool calib_flag;
	std::vector<std::vector<std::string>> calib_filenames;
	nvstitchVideoRigProperties_t calibrated_rig_properties{};
	std::vector<nvstitchCameraProperties_t> calibrated_cam_properties;
	bool audio_flag;
	nvstitchAudioRigProperties_t audio_rig_properties;
	std::vector<nvstitchAudioPayload_t> audio_payloads;
	std::string audio_type;
	std::string rtmp_addr;
	bool record_flag;
	std::string record_path;
	bool use_calibrate;
} appParams;

#define MAX_ENCODE_QUEUE 32
#define SET_VER(configStruct, type) {configStruct.version = type##_VER;}
template<class T>
class CNvQueue {
	T** m_pBuffer;
	unsigned int m_uSize;
	unsigned int m_uPendingCount;
	unsigned int m_uAvailableIdx;
	unsigned int m_uPendingndex;
public:
	CNvQueue() : m_pBuffer(NULL), m_uSize(0), m_uPendingCount(0), m_uAvailableIdx(0),
		m_uPendingndex(0)
	{
	}

	~CNvQueue()
	{
		delete[] m_pBuffer;
	}

	bool Initialize(T *pItems, unsigned int uSize)
	{
		m_uSize = uSize;
		m_uPendingCount = 0;
		m_uAvailableIdx = 0;
		m_uPendingndex = 0;
		m_pBuffer = new T *[m_uSize];
		for (unsigned int i = 0; i < m_uSize; i++)
		{
			m_pBuffer[i] = &pItems[i];
		}
		return true;
	}

	T * GetAvailable()
	{
		T *pItem = NULL;
		if (m_uPendingCount == m_uSize)
		{
			return NULL;
		}
		pItem = m_pBuffer[m_uAvailableIdx];
		m_uAvailableIdx = (m_uAvailableIdx + 1) % m_uSize;
		m_uPendingCount += 1;
		return pItem;
	}

	void incPending()
	{
		m_uPendingCount += 1;
	}

	void decPending()
	{
		m_uPendingCount -= 1;
	}

	bool isAllPending()
	{
		return m_uPendingCount == m_uSize;
	}

	T* GetPending()
	{
		if (m_uPendingCount == 0)
		{
			return NULL;
		}

		T *pItem = m_pBuffer[m_uPendingndex];
		m_uPendingndex = (m_uPendingndex + 1) % m_uSize;
		m_uPendingCount -= 1;
		return pItem;
	}
};

class CCudaAutoLock
{
private:
	CUcontext m_pCtx;
public:
	CCudaAutoLock(CUcontext pCtx) :m_pCtx(pCtx) { cuCtxPushCurrent(m_pCtx); };
	~CCudaAutoLock() { CUcontext cuLast = NULL; cuCtxPopCurrent(&cuLast); };
};
typedef enum
{
	NV_ENC_DX9 = 0,
	NV_ENC_DX11 = 1,
	NV_ENC_CUDA = 2,
	NV_ENC_DX10 = 3,
} NvEncodeDeviceType;


class VideoSource;
class AudioCap;
class app
{
public:
	nvstitchResult run(appParams *params);
	nvstitchResult calibrate(appParams *params);

	nvstitchResult feedVideoData(int index, void* pData, int size, int64_t timestamp); //todel
	nvstitchResult feedAudioData(int index, void* pData, int size, int64_t timestamp); 
	nvstitchResult encodeAAC(const void* pData, int size, int64_t timestamp);

	int processStitchedOutput(unsigned char* buffer, int bufsize, int64_t timestamp);

	void stitch_thread();
	void stitch_out_thread(); //not use
	void stitch_audio_thread();
	void encode_thread();

	int onAudioStitchedOutput(unsigned char* buffer, int bufsize, int64_t timestamp);

private:
	nvstitchResult getStitchedOut();
	int initNVEncode();
	NVENCSTATUS     AllocateIOBuffers(uint32_t uInputWidth, uint32_t uInputHeight, NV_ENC_BUFFER_FORMAT inputFormat);
	NVENCSTATUS     ReleaseIOBuffers();

	int initAACEncode();

	nvssVideoHandle stitcher = NULL;
	appParams *m_params;
	std::vector<VideoSource*> s_videoSources;
	bool  calibrated_ = false;

	AudioCap *audioCap_ = NULL;
	nvsf_t nvsfContext_ = NULL;
	nvsfInputDescriptor_t* inputs_=NULL;
	nvsfInput_t* inputHandles_=NULL;
	float * outBuffers_[2] = { 0 };
	short *outPcmBuffer = NULL;
	int updateAudioFrame_[16] = { 0 };

	CRITICAL_SECTION    vCriticalSection_;
	CRITICAL_SECTION    aCriticalSection_;
	CRITICAL_SECTION    rtmpCriticalSection_;

	void* rtmp_;
	bool srs_write_video_;
	unsigned int startMS;

	//MP4FileHandle mp4fileHandle_;
	//MP4TrackId mp4VideoTrack_;
	//MP4TrackId mp4AudioTrack_;
	flv_t *flvHandle_;
	
	pthread_t stitch_thread_ptr=0;
	int bStitchThreadExit;

	pthread_t stitch_out_thread_ptr=0;
	int bStitchOutThreadExit;
	pthread_cond_t has_stitch = PTHREAD_COND_INITIALIZER;  
	pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; 
	
	pthread_t stitch_audio_thread_ptr=0;
	int bStitchAudioThreadExit;

	pthread_t encode_thread_ptr=0;
	int bEncodeThreadExit;

	CUVIDEOFORMAT	stFormat_;
	std::vector<FrameQueue*>    pFrameQueues;
	std::vector<VideoParser*>   pVideoParsers;
	std::vector<VideoDecoder*>  pVideoDecoders;
	CUcontext          oContext_;
	CUdevice           oDevice_;
	CUvideoctxlock     oCtxLock_[4];
	CUdeviceptr    pCudaFrame_;
	CUdeviceptr    pCudaFrameNV12_;
	unsigned int   *pFrameYUV_;
	//BYTE          *pFrameRGBA_;

	CNvHWEncoder    *m_pNvHWEncoder;
	uint32_t                                             m_uEncodeBufferCount;
	EncodeConfig                                         m_stEncoderInput;
	EncodeBuffer                                         m_stEncodeBuffer[MAX_ENCODE_QUEUE];
	CNvQueue<EncodeBuffer>                               m_EncodeBufferQueue;
	EncodeOutputBuffer                                   m_stEOSOutputBfr;

	HANDLE_AACENCODER   m_aacEncHandle;
	uint8_t m_aacOutbuf[20480];
	//int imgcnt=0;//to del

};