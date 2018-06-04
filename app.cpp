
#include "app.h"

#include <fstream>
#include <vector>
#include <iostream>
#include <chrono>
#include <string>
#include <time.h>
#include "dynlink_cuda.h"    // <cuda.h>
//#include "cuda_runtime.h"

//#include "image_io_util.hpp"
#include "VideoSource.h"

#include "srs_librtmp.h"

#include "helper_functions.h"
#include "helper_cuda_drvapi.h"
#include "dynlink_builtin_types.h"      // <builtin_types.h>

#include "cudaProcessFrame.h"
#include "cudaModuleMgr.h"



CUmoduleManager   *g_pCudaModule;
CUfunction         g_kernelNV12toARGB = 0;
bool                g_bUpdateCSC = true;
CUstream            g_ReadbackSID = 0, g_KernelSID = 0;  //

eColorSpace        g_eColorSpace = ITU601;
float              g_nHue = 0.0f;

using std::chrono::milliseconds;
using std::chrono::high_resolution_clock;

static unsigned int msecond()
{
	timeval tv;
	gettimeofday(&tv,NULL);
	return tv.tv_sec*1000 + tv.tv_usec/1000;
}

static void InitializeCriticalSection(CRITICAL_SECTION *pCS)
{
	pthread_mutex_init(pCS, NULL);
}

static void EnterCriticalSection(CRITICAL_SECTION *pCS)
{
	pthread_mutex_lock(pCS);
}

static void LeaveCriticalSection(CRITICAL_SECTION *pCS)
{
	pthread_mutex_unlock(pCS);
}

// This is the CUDA stage for Video Post Processing.  Last stage takes care of the NV12 to ARGB
static void cudaPostProcessFrame(CUdeviceptr *ppDecodedFrame, size_t nDecodedPitch, int nBytesPerSample,
	CUdeviceptr *ppTextureData, size_t nTexturePitch, uint32 nWidth, uint32 nHeight,
	CUmodule cuModNV12toARGB,
	CUfunction fpCudaKernel, CUstream streamID)
{
	// Upload the Color Space Conversion Matrices
	if (g_bUpdateCSC)
	{
		// CCIR 601/709
		float hueColorSpaceMat[9];
		setColorSpaceMatrix(g_eColorSpace, hueColorSpaceMat, g_nHue);
		updateConstantMemory_drvapi(cuModNV12toARGB, hueColorSpaceMat);

		g_bUpdateCSC = false;
	}

	// TODO: Stage for handling video post processing

	// Final Stage: NV12toARGB color space conversion
	CUresult eResult;
	eResult = cudaLaunchNV12toARGBDrv(*ppDecodedFrame, nDecodedPitch, nBytesPerSample,
		*ppTextureData, nTexturePitch,
		nWidth, nHeight, fpCudaKernel, streamID);
}

nvstitchResult
app::calibrate(appParams *params)
{
	nvstitchResult res = NVSTITCH_SUCCESS;

	// Create rig instance
	RETURN_NVSTITCH_ERROR(nvstitchCreateVideoRigInstance(&params->rig_properties, &params->stitcher_properties.video_rig));

	uint32_t frame_count = (uint32_t)params->calib_filenames.size();
	uint32_t camera_count = (uint32_t)params->calib_filenames.at(0).size();

	// Create calibration instance
	nvstitchCalibrationProperties_t calib_prop{};
	calib_prop.version = NVSTITCH_VERSION;
	calib_prop.frame_count = frame_count;
	calib_prop.input_form = nvstitchMediaForm::NVSTITCH_MEDIA_FORM_HOST_BUFFER;
	calib_prop.input_format = nvstitchMediaFormat::NVSTITCH_MEDIA_FORMAT_RGBA8UI;  //NVSTITCH_MEDIA_FORMAT_RGBA8UI
	calib_prop.rig_estimate = params->stitcher_properties.video_rig;
	uint32_t input_image_channels =  4;

	nvstitchCalibrationInstanceHandle h_calib;
	RETURN_NVSTITCH_ERROR(nvstitchCreateCalibrationInstance(calib_prop, &h_calib));

	// Read input images for calibration
	uint32_t camera_num = params->rig_properties.num_cameras;

	const auto calibration_start = high_resolution_clock::now();

	/*for (uint32_t frame_index = 0; frame_index < 1; frame_index++)  lihengz
	{
		for (uint32_t cam_index = 0; cam_index < camera_count; cam_index++)
		{
			std::string image_file_path = params->input_dir_base + params->calib_filenames[frame_index][cam_index];

			unsigned char* rgba_bitmap_ptr = nullptr;
			int image_width, image_height;
			if (getRgbaImage(image_file_path, &rgba_bitmap_ptr, image_width, image_height) == false)
			{
				std::cout << "Error reading calibration image " << image_file_path << endl;
				return NVSTITCH_ERROR_MISSING_FILE;
			}

			if (nullptr == rgba_bitmap_ptr)
			{
				std::cout << "Error reading input image:" << image_file_path << std::endl;
				return  NVSTITCH_ERROR_NULL_POINTER;
			}

			uint32_t width = params->rig_properties.cameras[cam_index].image_size.x;
			uint32_t height = params->rig_properties.cameras[cam_index].image_size.y;

			nvstitchPayload_t calib_payload = nvstitchPayload_t{ calib_prop.input_form,{ width, height } };

			calib_payload.payload.buffer.ptr = rgba_bitmap_ptr;

			calib_payload.payload.buffer.pitch = params->rig_properties.cameras[cam_index].image_size.x * input_image_channels;

			RETURN_NVSTITCH_ERROR(nvstitchFeedCalibrationInput(frame_index, h_calib, cam_index, &calib_payload));
		}
	}*/

	// Calibrate
	nvstitchVideoRigHandle h_calibrated_video_rig;
	RETURN_NVSTITCH_ERROR(nvstitchCalibrate(h_calib, &h_calibrated_video_rig));

	// Report calibration time.
	auto time = std::chrono::duration_cast<milliseconds>(high_resolution_clock::now() - calibration_start).count();
	std::cout << "Calibration Time: " << time << " ms" << std::endl;

	// Fetch calibrated rig properties
	RETURN_NVSTITCH_ERROR(nvstitchGetVideoRigProperties(h_calibrated_video_rig, &params->calibrated_rig_properties));

	// Destroy calibration instance
	RETURN_NVSTITCH_ERROR(nvstitchDestroyCalibrationInstance(h_calib));

	return NVSTITCH_SUCCESS;
}

//FILE *fp_test = fopen("test.264", "wb"); //lihengz
//! [Stitcher output]
void NVSTITCHCALLBACK onStitchedOutput(unsigned char* buffer, int bufsize, int64_t timestamp,
	void* app_data)
{
	app *pApp = (app *)app_data;
	if (pApp)
		pApp->processStitchedOutput(buffer, bufsize, timestamp);
	/*if (out_payload && out_payload->payload.frame.ptr)
	{
		fwrite(out_payload->payload.frame.ptr, 1, out_payload->payload.frame.size, fp_test);
		fflush(fp_test);
	}*/
	
}
//! [Stitcher output]

int app::processStitchedOutput(unsigned char* buffer, int bufsize, int64_t timestamp)
{
	unsigned int nowMs = msecond();
	if (buffer && bufsize>0)
	{
		if (rtmp_)
		{
			EnterCriticalSection(&rtmpCriticalSection_);
			int ret = srs_h264_write_raw_frames(rtmp_, (char *)buffer,
				bufsize, nowMs - startMS, nowMs - startMS); //out_payload->payload.frame.timestamp/10000  
			if (ret != 0) {
				if (srs_h264_is_dvbsp_error(ret)) {
					srs_human_trace("ignore drop video error, code=%d", ret);
				}
				else if (srs_h264_is_duplicated_sps_error(ret)) {
					srs_human_trace("ignore duplicated sps, code=%d", ret);
				}
				else if (srs_h264_is_duplicated_pps_error(ret)) {
					srs_human_trace("ignore duplicated pps, code=%d", ret);
				}
				else {
					srs_human_trace("send h264 raw data failed. ret=%d", ret);
					//goto rtmp_destroy;
				}
			}
			srs_write_video_ = true;
			LeaveCriticalSection(&rtmpCriticalSection_);
		}
		
		/*if (mp4VideoTrack_ && m_params->record_flag)
		{
			if (out_payload->payload.frame.size > 4)
			{
				uint32_t* p = (uint32_t*)out_payload->payload.frame.ptr;
				*p = htonl(out_payload->payload.frame.size - 4);
			}
			MP4WriteSample(mp4fileHandle_, mp4VideoTrack_, (const uint8_t *)out_payload->payload.frame.ptr, out_payload->payload.frame.size, MP4_INVALID_DURATION, 0, 1);
		}*/
		if (flvHandle_)
		{
			int iskeyframe = ((uint8_t *)buffer)[4] == 0x67 || ((uint8_t *)buffer)[4] == 0x65;
			flv_write_video_packet(flvHandle_, iskeyframe, (uint8_t *)buffer, bufsize, nowMs - startMS); /// 10000
		}
	}

	return 0;
}

void *stitchAudioProc(void* lpParam)
{
	app *pApp=(app*)lpParam;
	pApp->stitch_audio_thread();
}

void *stitchProc(void* lpParam)
{
	app *pApp=(app*)lpParam;
	pApp->stitch_thread();
}

void *encodeProc(void* lpParam)
{
	app *pApp=(app*)lpParam;
	pApp->encode_thread();
}


nvstitchResult
app::run(appParams *params)
{
	InitializeCriticalSection(&vCriticalSection_);
	InitializeCriticalSection(&aCriticalSection_);
	InitializeCriticalSection(&rtmpCriticalSection_);
	srs_write_video_ = false;
	rtmp_ = NULL;
	//mp4fileHandle_ = NULL;
	//mp4VideoTrack_ = 0;
	//mp4AudioTrack_ = 0;
	flvHandle_ = NULL;
	m_aacEncHandle = NULL;

	/*************mp4file******************************/
	char filename[255];
	if (params->record_flag)
	{
		time_t rawtime;
		struct tm * timeinfo;
		char timestr[100];
		time(&rawtime);
		timeinfo = localtime(&rawtime);
		strftime(timestr, sizeof(timestr), "%Y%m%d-%H%M%S", timeinfo);

		sprintf(filename, "%s/Mugo_%s.flv", params->record_path.c_str(), timestr);
		flvHandle_= flv_init( filename, 30, params->stitcher_properties.output_payloads->image_size.x, params->stitcher_properties.output_payloads->image_size.y);
		/*sprintf(filename, "%s/test.mp4", params->record_path.c_str());
		mp4fileHandle_ = MP4Create(filename);//创建mp4文件
		if (mp4fileHandle_ == MP4_INVALID_FILE_HANDLE)
		{
			printf("open file fialed.\n");
		}

		MP4SetTimeScale(mp4fileHandle_, 90000);

		//添加h264 track    
		mp4VideoTrack_ = MP4AddH264VideoTrack(mp4fileHandle_, 90000, 90000 / 25, 3840, 2160,
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
		if (params->audio_flag)
		{
			mp4AudioTrack_ = MP4AddAudioTrack(mp4fileHandle_, 44100, 1024, MP4_MPEG4_AUDIO_TYPE);
			if (mp4AudioTrack_ == MP4_INVALID_TRACK_ID)
			{
				printf("add audio track failed.\n");
			}
			MP4SetAudioProfileLevel(mp4fileHandle_, 0x2);
		}

		//MP4Close(mp4fileHandle_);
		//mp4fileHandle_=MP4Modify(filename); */
	}

	/*******rtmp**************************************/
	if (!params->rtmp_addr.empty())
	{
		do {
			rtmp_ = srs_rtmp_create(params->rtmp_addr.c_str()); //"rtmp://192.168.1.99/live/1"

			if (srs_rtmp_handshake(rtmp_) != 0) {
				srs_human_trace("simple handshake failed.");
				continue;// break;
			}
			srs_human_trace("simple handshake success");

			if (srs_rtmp_connect_app(rtmp_) != 0) {
				srs_human_trace("connect vhost/app failed.");
				continue;//break;
			}
			srs_human_trace("connect vhost/app success");

			if (srs_rtmp_publish_stream(rtmp_) != 0) {
				srs_human_trace("publish stream failed.");
				continue;//break;
			}
			srs_human_trace("publish stream success");
			break;
		} while (1);
	}
	startMS = msecond();
	/*********************************************************/

	nvstitchResult res = NVSTITCH_SUCCESS;
	m_params = params;


	// Initialize stitcher instance
	//nvssVideoHandle stitcher;
	//RETURN_NVSS_ERROR(nvssVideoCreateInstance(&stitcher_props, &params->rig_properties, &stitcher));

	const auto stitch_start = high_resolution_clock::now();

	// Setup input
	// Loop for every video input
	s_videoSources.resize(params->rig_properties.num_cameras);
	for (int i = 0; i < params->rig_properties.num_cameras; i++)
	{
		s_videoSources[i] = new VideoSource();

		char rtspurl[255];
		//if(i==0)
		//	sprintf(rtspurl, "rtsp://192.168.1.88/av0_0");
		//else
		sprintf(rtspurl, "rtsp://192.168.1.1%d/av0_0", i+1 ); //params->rig_properties.num_cameras - i
		s_videoSources[i]->init(rtspurl, i, this, params->audio_type=="ipcam");
		if (params->record_flag)
			s_videoSources[i]->setRecordPath(params->record_path);

		//s_videoSources[i]->start();
	}
	if (params->audio_flag )
	{
		initAACEncode();
		if (params->audio_type == "mic")
		{
			/*audioCap_ = new AudioCap(0, this);
			audioCap_->StartCap();*/
		}
		else
		{
			const uint32_t processingSize = 1024;
			float stereoSpread = 0.5;

			NVSF_CALL(nvsfInitialize());
			uint32_t version;
			NVSF_CALL(nvsfGetVersion(&version));

			NVSF_CALL(nvsfCreateContext(&nvsfContext_, "", 0));
			NVSF_CALL(nvsfSetSampleRate(nvsfContext_, params->stitcher_properties.audio_output_format->sampleRate));
			NVSF_CALL(nvsfSetOutputFormat(nvsfContext_, NVSTITCH_AUDIO_OUTPUT_STEREO_MIXDOWN));
			NVSF_CALL(nvsfSetPullSize(nvsfContext_, processingSize));
			NVSF_CALL(nvsfSetAlgorithmParameter(nvsfContext_, NVSF_OUTPUT_GAIN, &params->stitcher_properties.audio_output_gain));
			NVSF_CALL(nvsfSetAlgorithmParameter(nvsfContext_, NVSF_STEREO_SPREAD_MIX_COEFFICIENT, &stereoSpread));

			uint32_t numInputs = params->audio_rig_properties.num_sources;
			inputs_ = new nvsfInputDescriptor_t[numInputs];
			inputHandles_ = new nvsfInput_t[numInputs];
			for (int i = 0; i < numInputs; i++)
			{
				inputs_[i].numChannels = 1;
				//memset(&inputs_[i].pose, 0, sizeof(nvstitchPose_t));//to modify  lihengz
				memcpy(&inputs_[i].pose, &params->audio_rig_properties.sources[i].pose, sizeof(nvstitchPose_t));
				inputs_[i].type = NVSTITCH_AUDIO_INPUT_TYPE_OMNI;
				NVSF_CALL(nvsfAddInput(nvsfContext_, &inputHandles_[i], &inputs_[i]));
			}
			NVSF_CALL(nvsfCommitConfiguration(nvsfContext_));

			outBuffers_[0] = new float[processingSize];
			outBuffers_[1] = new float[processingSize];
			outPcmBuffer = new short[processingSize*2];

			//start stitch audio thread
			bStitchAudioThreadExit = FALSE;
			pthread_create(&stitch_audio_thread_ptr, NULL, stitchAudioProc, (void*)this);
		}
	}

	//====================================================================
	// Initialize the CUDA and NVDECODE
	typedef void *CUDADRIVER;
	CUDADRIVER hHandleDriver = 0;
	CUresult cuResult;
	cuResult = cuInit(0, __CUDA_API_VERSION, hHandleDriver);
	cuResult = cuvidInit(0);

	CUdevice cuda_device = gpuGetMaxGflopsDeviceIdDRV();
	checkCudaErrors(cuDeviceGet(&oDevice_, cuda_device));
	checkCudaErrors(cuCtxCreate(&oContext_, CU_CTX_BLOCKING_SYNC, oDevice_));

	cuCtxPushCurrent(oContext_);
	/*try
	{
		char FilePath[MAX_PATH + 1] = { 0 };
		char *p = NULL;
		GetModuleFileNameA(NULL, FilePath, sizeof(FilePath));
		p = strrchr(FilePath, '\\');
		*p = '\0';

		// Initialize CUDA releated Driver API (32-bit or 64-bit), depending the platform running
		g_pCudaModule = new CUmoduleManager("NV12ToARGB_drvapi_x64.ptx", FilePath, 2, 2, 2);
	}
	catch (char const *p_file)
	{
		// If the CUmoduleManager constructor fails to load the PTX file, it will throw an exception
		printf("\n>> CUmoduleManager::Exception!  %s not found!\n", p_file);
		printf(">> Please rebuild NV12ToARGB_drvapi.cu or re-install this sample.\n");
	}*/

	g_pCudaModule = new CUmoduleManager("NV12ToARGB_drvapi_x64.ptx", "./", 2, 2, 2);
	g_pCudaModule->GetCudaFunction("NV12ToARGB_drvapi", &g_kernelNV12toARGB);

	CUresult result;

	memset(&stFormat_, 0, sizeof(CUVIDEOFORMAT));
	stFormat_.codec = cudaVideoCodec_H264;
	stFormat_.chroma_format = cudaVideoChromaFormat_420;
	stFormat_.progressive_sequence = true;
	stFormat_.coded_width = params->rig_properties.cameras->image_size.x;
	stFormat_.coded_height = params->rig_properties.cameras->image_size.y;

	stFormat_.display_area.right = params->rig_properties.cameras->image_size.x;
	stFormat_.display_area.left = 0;
	stFormat_.display_area.bottom = params->rig_properties.cameras->image_size.y;
	stFormat_.display_area.top = 0;

	CUVIDEOFORMATEX oFormatEx;
	memset(&oFormatEx, 0, sizeof(CUVIDEOFORMATEX));
	oFormatEx.format = stFormat_;

	pFrameQueues.resize(params->rig_properties.num_cameras);
	pVideoParsers.resize(params->rig_properties.num_cameras);
	pVideoDecoders.resize(params->rig_properties.num_cameras);
	for (int i = 0; i < params->rig_properties.num_cameras; i++)
	{
		// bind the context lock to the CUDA context
		result = cuvidCtxLockCreate(&oCtxLock_[i], oContext_);
		if (result != CUDA_SUCCESS)
		{
			printf("cuvidCtxLockCreate failed: %d\n", result);
			assert(0);
		}

		pFrameQueues[i]= new CUVIDFrameQueue(oCtxLock_[i]);
		pVideoDecoders[i] = new VideoDecoder(stFormat_, oContext_, cudaVideoCreate_PreferCUVID, oCtxLock_[i]);
		pVideoParsers[i] = new VideoParser(pVideoDecoders[i], pFrameQueues[i], &oFormatEx);
		s_videoSources[i]->setParser(*pVideoParsers[i], oContext_,0);

	}

	checkCudaErrors(cuStreamCreate(&g_KernelSID, 0));
	checkCudaErrors(cuStreamCreate(&g_ReadbackSID, 0));

	checkCudaErrors(cuMemAlloc(&pCudaFrame_, (pVideoDecoders[0]->targetWidth() *4+1 ) * pVideoDecoders[0]->targetHeight() ));
	checkCudaErrors(cuMemAlloc(&pCudaFrameNV12_, pVideoDecoders[0]->targetWidth() * pVideoDecoders[0]->targetHeight()*3/2));
	checkCudaErrors(result = cuMemAllocHost((void **)&pFrameYUV_, pVideoDecoders[0]->targetWidth() * pVideoDecoders[0]->targetHeight() * 4));
	//checkCudaErrors(result = cuMemAllocHost((void **)&pFrameRGBA_, pVideoDecoders[0]->targetWidth() * pVideoDecoders[0]->targetHeight() * 4));
	
	cuCtxPopCurrent(NULL);

	initNVEncode();

	/******init nvss*****************************************/
	if (!params->use_calibrate)
	{
		//int num_gpus;
		//cudaGetDeviceCount(&num_gpus);

		std::vector<int> gpus;
		gpus.reserve(1);  //num_gpus
		gpus.push_back(0);

		/*for (int gpu = 0; gpu < num_gpus; ++gpu)
		{
			cudaDeviceProp prop;
			cudaGetDeviceProperties(&prop, gpu);

			// Require minimum compute 5.2
			if (prop.major > 5 || (prop.major == 5 && prop.minor >= 2))
			{
				gpus.push_back(gpu);

				// Multi-GPU not yet supported for mono, so just take the first GPU
				//if (!params->stereo_flag)
				break;
			}
		}*/
		nvssVideoStitcherProperties_t stitcher_props{ 0 };
		stitcher_props.version = NVSTITCH_VERSION;
		stitcher_props.pano_width = m_params->stitcher_properties.output_payloads->image_size.x;
		stitcher_props.quality = m_params->stitcher_properties.video_pipeline_options->stitch_quality;
		stitcher_props.num_gpus = gpus.size();
		stitcher_props.ptr_gpus = gpus.data();

		stitcher_props.pipeline = NVSTITCH_STITCHER_PIPELINE_MONO;
		stitcher_props.feather_width = 2.0f;

		CHECK_NVSS_ERROR(nvssVideoCreateInstance(&stitcher_props, &m_params->calibrated_rig_properties, &stitcher));

		calibrated_ = true;
	}
	//--------------------------------------------------------

	//start stitch thread
	bStitchThreadExit = FALSE;
	pthread_create(&stitch_thread_ptr, NULL, stitchProc, (void*)this);

	//start encode thread	
	bEncodeThreadExit = FALSE;
	pthread_create(&encode_thread_ptr, NULL, encodeProc, (void*)this);
	//--------------------------------------------------------------------
	
	for (int i = 0; i < params->rig_properties.num_cameras; i++)
	{
		s_videoSources[i]->start();
	}

	//cv::waitKey();
	while ( getchar() != 'q') {
		usleep(1000*1000);
	}

	for (int i = 0; i < params->rig_properties.num_cameras; i++)
	{
		delete s_videoSources[i];
	}

	bStitchThreadExit = TRUE;
	if (stitch_thread_ptr)
	{
		pthread_join(stitch_thread_ptr, NULL);
		stitch_thread_ptr = 0;
	}

	bStitchAudioThreadExit = TRUE;
	if (stitch_audio_thread_ptr)
	{
		pthread_join(stitch_audio_thread_ptr, NULL);
		stitch_audio_thread_ptr = 0;
	}

	bEncodeThreadExit = TRUE;
	if (encode_thread_ptr)
	{
		pthread_join(encode_thread_ptr, NULL);
		encode_thread_ptr = 0;
	}

	ReleaseIOBuffers();
	if (m_pNvHWEncoder)
	{
		NVENCSTATUS nvStatus = m_pNvHWEncoder->NvEncDestroyEncoder();
		delete m_pNvHWEncoder;
	}

	if(m_aacEncHandle)
		aacEncClose(&m_aacEncHandle);
	if (nvsfContext_)
	{
		NVSF_CALL(nvsfDestroyContext(nvsfContext_));
		NVSF_CALL(nvsfFinalize());
	}
	delete[] outBuffers_[0];
	delete[] outBuffers_[1];
	delete[] outPcmBuffer;
	delete[] inputHandles_;
	delete[] inputs_;

	for (int i = 0; i < params->rig_properties.num_cameras; i++)
	{
		delete pVideoParsers[i];
		delete pVideoDecoders[i];
		delete pFrameQueues[i];
	}


	/*if (oCtxLock_)
	{
		checkCudaErrors(cuvidCtxLockDestroy(oCtxLock_));
	}

	if (oContext_ )
	{
		checkCudaErrors(cuCtxDestroy(oContext_));
		oContext_ = NULL;
	}*/

	// Report stitch time
	auto time = std::chrono::duration_cast<milliseconds>(high_resolution_clock::now() - stitch_start).count();
	std::cout << "Stitch Time: " << time << " ms" << std::endl;

	
	// Clean up
	RETURN_NVSS_ERROR(nvssVideoDestroyInstance(stitcher));

	//if(mp4fileHandle_)
	//	MP4Close(mp4fileHandle_);
	//MP4Optimize(filename);
	if (flvHandle_)
		flv_write_trailer(flvHandle_);

	if(rtmp_)
		srs_rtmp_destroy(rtmp_);

	return NVSTITCH_SUCCESS;
}


void app::stitch_thread()
{
	char *updateFrame = new char[m_params->rig_properties.num_cameras];
	memset(updateFrame, 0, sizeof(char)*m_params->rig_properties.num_cameras);
	int updateFrameNum = 0;

	//-------------------------calibrate-----------------------------------------------
	nvstitchCalibrationProperties_t calib_prop{};
	nvstitchCalibrationInstanceHandle h_calib;
	if (m_params->use_calibrate)
	{
		// Create rig instance
		CHECK_NVSS_ERROR(nvstitchCreateVideoRigInstance(&m_params->rig_properties, &m_params->stitcher_properties.video_rig));

		uint32_t frame_count = (uint32_t)m_params->calib_filenames.size();

		// Create calibration instance
		
		calib_prop.version = NVSTITCH_VERSION;
		calib_prop.frame_count = 1;// frame_count;
		calib_prop.input_form = nvstitchMediaForm::NVSTITCH_MEDIA_FORM_HOST_BUFFER;// NVSTITCH_MEDIA_FORM_DEVICE_BUFFER; // NVSTITCH_MEDIA_FORM_HOST_BUFFER;
		calib_prop.input_format = nvstitchMediaFormat::NVSTITCH_MEDIA_FORMAT_RGBA8UI;  //NVSTITCH_MEDIA_FORMAT_RGBA8UI
		calib_prop.rig_estimate = m_params->stitcher_properties.video_rig;
		uint32_t input_image_channels = 4;

		
		CHECK_NVSS_ERROR(nvstitchCreateCalibrationInstance(calib_prop, &h_calib));
	}
	//=================================================================================

	while (!bStitchThreadExit)
	{
		for (int i = 0; i < m_params->rig_properties.num_cameras; i++)
		{
			CUVIDPARSERDISPINFO oDisplayInfo;

			if (!updateFrame[i] && pFrameQueues[i]->dequeue(&oDisplayInfo))
			{
				//CCtxAutoLock lck(oCtxLock_[i]);
				// Push the current CUDA context (only if we are using CUDA decoding path)
				//CUresult result = cuCtxPushCurrent(oContext_);
				CUdeviceptr  pDecodedFrame;

				CUVIDPROCPARAMS oVideoProcessingParameters;
				memset(&oVideoProcessingParameters, 0, sizeof(CUVIDPROCPARAMS));
				oVideoProcessingParameters.progressive_frame = oDisplayInfo.progressive_frame;
				oVideoProcessingParameters.second_field = 0;
				oVideoProcessingParameters.top_field_first = oDisplayInfo.top_field_first;
				oVideoProcessingParameters.unpaired_field = (oDisplayInfo.progressive_frame == 1 || oDisplayInfo.repeat_first_field <= 1);

				unsigned int nDecodedPitch = 0;
				// map decoded video frame to CUDA surfae
				if (pVideoDecoders[i]->mapFrame(oDisplayInfo.picture_index, &pDecodedFrame, &nDecodedPitch, &oVideoProcessingParameters) != CUDA_SUCCESS)
				{
					// release the frame, so it can be re-used in decoder
					pFrameQueues[i]->releaseFrame(&oDisplayInfo);
					std::cout << "Error mapFrame pVideoDecoders" << std::endl;
					// Detach from the Current thread
					//checkCudaErrors(cuCtxPopCurrent(NULL));
					continue;
				}

				if (!calibrated_)
				{
					cuvidCtxLock(oCtxLock_[i], 0);
					cudaPostProcessFrame(&pDecodedFrame, nDecodedPitch, pVideoDecoders[i]->GetNumBytesPerSample(), &pCudaFrame_,
						pVideoDecoders[i]->targetWidth() * 4, pVideoDecoders[i]->targetWidth(), pVideoDecoders[i]->targetHeight(),
						g_pCudaModule->getModule(), g_kernelNV12toARGB, g_KernelSID); //g_pCudaModule->getModule()
					CUresult result = cuMemcpyDtoHAsync(pFrameYUV_, pCudaFrame_, (pVideoDecoders[i]->targetWidth() * pVideoDecoders[i]->targetHeight() * 4), g_ReadbackSID);
					if (result != CUDA_SUCCESS)
					{
						printf("cuMemAllocHost returned %d\n", (int)result);
						checkCudaErrors(result);
					}
					checkCudaErrors(cuStreamSynchronize(g_ReadbackSID));
					/*if (cudaMemcpy2D(pFrameYUV_, pVideoDecoders[i]->targetWidth() *4,
						(void*)pCudaFrame_, pVideoDecoders[i]->targetWidth() * 4,
						pVideoDecoders[i]->targetWidth()*4, pVideoDecoders[i]->targetHeight(),
						cudaMemcpyDeviceToHost) != cudaSuccess)
					{
						std::cout << "Error copying output stacked panorama from CUDA buffer" << std::endl;
					}*/
					cuvidCtxUnlock(oCtxLock_[i], 0);

					/******test to del***********/
					char filename[256];
					sprintf(filename, "./img/%d.bmp", i);
					//putRgbaImage(filename, pFrameYUV_, pVideoDecoders[i]->targetWidth(), pVideoDecoders[i]->targetHeight());  lihengz
					//----------------------------

					nvstitchPayload_t calib_payload = nvstitchPayload_t{ calib_prop.input_form,{(uint32_t)pVideoDecoders[i]->targetWidth(), (uint32_t)pVideoDecoders[i]->targetHeight() } };

					calib_payload.payload.buffer.ptr = (void*)pFrameYUV_;

					calib_payload.payload.buffer.pitch = pVideoDecoders[i]->targetWidth() * 4;

					CHECK_NVSS_ERROR(nvstitchFeedCalibrationInput(0, h_calib, i, &calib_payload));
				}

				
				//checkCudaErrors(cuCtxPopCurrent(NULL));
				//feedVideoData(i, (void*)pFrameYUV_, pVideoDecoders[i]->targetWidth() * 4, oDisplayInfo.timestamp);
				else
				{
					nvstitchImageBuffer_t input_image;
					CHECK_NVSS_ERROR(nvssVideoGetInputBuffer(stitcher, i, &input_image));

					CUstream_st *inStreamID;
					CHECK_NVSS_ERROR(nvssVideoGetInputStream(stitcher, i, &inStreamID));
					cuStreamSynchronize(inStreamID);

					cuvidCtxLock(oCtxLock_[i], 0);
					
					if (nDecodedPitch < pVideoDecoders[i]->targetWidth())
					{
						std::cout << "Error nDecodedPitch:" << nDecodedPitch << std::endl;
					}
					cudaPostProcessFrame(&pDecodedFrame, nDecodedPitch, pVideoDecoders[i]->GetNumBytesPerSample(),
						(CUdeviceptr*)&input_image.dev_ptr,
						pVideoDecoders[i]->targetWidth() * 4, pVideoDecoders[i]->targetWidth(), pVideoDecoders[i]->targetHeight(),
						g_pCudaModule->getModule(), g_kernelNV12toARGB, g_KernelSID);
					cuStreamSynchronize(g_KernelSID);

					/*CUresult cudaerr;
					if (cudaMemcpy((void *)pCudaFrameNV12_, (void *)pDecodedFrame, pVideoDecoders[i]->targetWidth()*pVideoDecoders[i]->targetHeight() * 3 / 2,
						cudaMemcpyDeviceToDevice) != cudaSuccess)
					{
						std::cout << "Error copying output stacked panorama from CUDA buffer" << std::endl;
					}
					cudaPostProcessFrame(&pCudaFrameNV12_, nDecodedPitch, pVideoDecoders[i]->GetNumBytesPerSample(), &pCudaFrame_,
						pVideoDecoders[i]->targetWidth()*4 , pVideoDecoders[i]->targetWidth(), pVideoDecoders[i]->targetHeight(),
						g_pCudaModule->getModule(), g_kernelNV12toARGB, g_KernelSID);
					cuStreamSynchronize(g_KernelSID);

					if ((cudaerr = cudaMemcpy2D(input_image.dev_ptr, input_image.pitch,
						(void *)pCudaFrame_, pVideoDecoders[i]->targetWidth()*4,
						pVideoDecoders[i]->targetWidth()*4, pVideoDecoders[i]->targetHeight(),
						cudaMemcpyDeviceToDevice)) != cudaSuccess)
					{
						std::cout << "Error copying RGBA image bitmap between CUDA buffer ,err="<< cudaGetErrorString(cudaerr) << std::endl;
						cuvidCtxUnlock(oCtxLock_[i], 0);
						pVideoDecoders[i]->unmapFrame(pDecodedFrame);
						pFrameQueues[i]->releaseFrame(&oDisplayInfo);					
						continue;
						//return NVSTITCH_ERROR_GENERAL;
					}*/

					/*if (cudaMemcpy(pFrameYUV_, (void *)pDecodedFrame, pVideoDecoders[i]->targetWidth()*pVideoDecoders[i]->targetHeight()*3/2,
						cudaMemcpyDeviceToHost) != cudaSuccess)
					{
						std::cout << "Error copying output stacked panorama from CUDA buffer" << std::endl;
					}*/
					cuvidCtxUnlock(oCtxLock_[i], 0);

					/*YCrCb2RGBConver(pFrameYUV_, pFrameYUV_ + pVideoDecoders[i]->targetWidth()*pVideoDecoders[i]->targetHeight(),
						pVideoDecoders[i]->targetWidth(), pVideoDecoders[i]->targetWidth() / 2,
						pFrameRGBA_, pVideoDecoders[i]->targetWidth(), pVideoDecoders[i]->targetHeight(), 4);*/
				}

				// unmap video frame
				// unmapFrame() synchronizes with the VideoDecode API (ensures the frame has finished decoding)
				pVideoDecoders[i]->unmapFrame(pDecodedFrame);

				// Detach from the Current thread
				//checkCudaErrors(cuCtxPopCurrent(NULL));
				// release the frame, so it can be re-used in decoder
				pFrameQueues[i]->releaseFrame(&oDisplayInfo);

				updateFrame[i] = 1;
				updateFrameNum++;
				//i++;
			}//end if
		} //end for

		if(updateFrameNum == m_params->rig_properties.num_cameras)
		{
			if (!calibrated_)
			{
				//int num_gpus;
				//cudaGetDeviceCount(&num_gpus);

				std::vector<int> gpus;
				gpus.reserve(1);  //num_gpus
				gpus.push_back(0);

				/*for (int gpu = 0; gpu < num_gpus; ++gpu)
				{
					cudaDeviceProp prop;
					cudaGetDeviceProperties(&prop, gpu);

					// Require minimum compute 5.2
					if (prop.major > 5 || (prop.major == 5 && prop.minor >= 2))
					{
						gpus.push_back(gpu);

						// Multi-GPU not yet supported for mono, so just take the first GPU
						//if (!params->stereo_flag)
						break;
					}
				}*/
				nvssVideoStitcherProperties_t stitcher_props{ 0 };
				stitcher_props.version = NVSTITCH_VERSION;
				stitcher_props.pano_width = m_params->stitcher_properties.output_payloads->image_size.x;
				stitcher_props.quality = m_params->stitcher_properties.video_pipeline_options->stitch_quality;
				stitcher_props.num_gpus = gpus.size();
				stitcher_props.ptr_gpus = gpus.data();

				/*if (params->stereo_flag)
				{
				stitcher_props.pipeline = NVSTITCH_STITCHER_PIPELINE_STEREO;
				stitcher_props.stereo_ipd = 6.3f;
				}
				else */
				{
					stitcher_props.pipeline = NVSTITCH_STITCHER_PIPELINE_MONO;
					stitcher_props.feather_width = 2.0f;
				}

				// Calibrate
				nvstitchVideoRigHandle h_calibrated_video_rig;
				if (nvstitchCalibrate(h_calib, &h_calibrated_video_rig) == NVSTITCH_SUCCESS)
				{
					// Fetch calibrated rig properties
					if (nvstitchGetVideoRigProperties(h_calibrated_video_rig, &m_params->calibrated_rig_properties) == NVSTITCH_SUCCESS)
					{
						printf("======calibrate success=========\n");
						CHECK_NVSS_ERROR(nvssVideoCreateInstance(&stitcher_props, &m_params->calibrated_rig_properties, &stitcher));
					}
				}
				if (stitcher == NULL)
				{
					printf("======calibrate fail==========\n");
					CHECK_NVSS_ERROR(nvssVideoCreateInstance(&stitcher_props, &m_params->calibrated_rig_properties, &stitcher));
				}

				// Destroy calibration instance
				CHECK_NVSS_ERROR(nvstitchDestroyCalibrationInstance(h_calib));

				//clear frame queue
				for (int i = 0; i < m_params->rig_properties.num_cameras; i++)
				{
					CUVIDPARSERDISPINFO oDisplayInfo;
					while (pFrameQueues[i]->dequeue(&oDisplayInfo))
					{
						pFrameQueues[i]->releaseFrame(&oDisplayInfo);
					}
				}

				//start stitchout thread
				/*LPVOID arg_ = NULL;
				Common::ThreadCallback cb = BIND_MEM_CB(&app::stitch_out_thread, this);
				stitch_out_thread_ptr = new Common::CThread(cb, TRUE);
				if (!stitch_out_thread_ptr)
				{
					printf("create stitch_out_thread fail\n");
					//return NVSTITCH_SUCCESS;
				}
				bStitchOutThreadExit = FALSE;
				stitch_out_thread_ptr->start(arg_);*/


				calibrated_ = true;
			}
			else
			{
				/*for (int i = 0; i < m_params->rig_properties.num_cameras; i++)
				{
					CUstream_st *inStreamID;
					CHECK_NVSS_ERROR(nvssVideoGetInputStream(stitcher, i, &inStreamID));
					cudaStreamSynchronize(inStreamID);
				}*/
				// Stitch
				nvstitchResult pRes = nvssVideoStitch(stitcher);
				if(pRes == NVSTITCH_SUCCESS)
					getStitchedOut();
				else
					std::cerr << "Error at line " << __LINE__ << ": " << nvssVideoGetErrorString(pRes) << std::endl;
			}
			updateFrameNum = 0;
			memset(updateFrame, 0, sizeof(char)*m_params->rig_properties.num_cameras);
		}
		
	}
}

nvstitchResult app::getStitchedOut()
{
	// Synchronize CUDA before snapping start time
	//cudaStreamSynchronize(cudaStreamDefault);

		CUstream_st *outStreamID;
		RETURN_NVSS_ERROR(nvssVideoGetOutputStream(stitcher, NVSTITCH_EYE_MONO, &outStreamID));
		// Synchronize CUDA before snapping end time 
		cuStreamSynchronize(outStreamID);

		//unsigned char *out_stacked = nullptr;
		nvstitchImageBuffer_t output_image;
		RETURN_NVSS_ERROR(nvssVideoGetOutputBuffer(stitcher, NVSTITCH_EYE_MONO, &output_image));

		//encode
		EncodeBuffer *pEncodeBuffer = m_EncodeBufferQueue.GetAvailable(); 
		if (!pEncodeBuffer)
		{
			std::cout << "Error m_EncodeBufferQueue.GetAvailable" << std::endl;
			return NVSTITCH_ERROR_GENERAL;
		}
		/*if (cudaMemcpy2D((void*)pEncodeBuffer->stInputBfr.pNV12devPtr, pEncodeBuffer->stInputBfr.uNV12Stride,
			output_image.dev_ptr, output_image.pitch,
			output_image.row_bytes, output_image.height,
			cudaMemcpyDeviceToDevice) != cudaSuccess)*/
		if(cuMemcpyDtoD((CUdeviceptr)pEncodeBuffer->stInputBfr.pNV12devPtr,
			            (CUdeviceptr)output_image.dev_ptr, output_image.row_bytes * output_image.height) != CUDA_SUCCESS)
		{
			std::cout << "Error copying RGBA image bitmap to CUDA buffer" << std::endl;
			m_EncodeBufferQueue.incPending();
			return NVSTITCH_ERROR_GENERAL;
		}

		NVENCSTATUS nvStatus = m_pNvHWEncoder->NvEncMapInputResource(pEncodeBuffer->stInputBfr.nvRegisteredResource, &pEncodeBuffer->stInputBfr.hInputSurface);
		if (nvStatus != NV_ENC_SUCCESS)
		{
			PRINTERR("Failed to Map input buffer %p\n", pEncodeBuffer->stInputBfr.hInputSurface);
			return NVSTITCH_ERROR_GENERAL;
		}

		m_pNvHWEncoder->NvEncEncodeFrame(pEncodeBuffer, NULL, m_stEncoderInput.width, m_stEncoderInput.height);

		m_EncodeBufferQueue.incPending();
	

	return NVSTITCH_SUCCESS;
}


void app::stitch_out_thread()  //not use
{
	// Synchronize CUDA before snapping start time
	//cudaStreamSynchronize(cudaStreamDefault);

	while (!bStitchOutThreadExit)
	{
		CUstream_st *outStreamID;
		CHECK_NVSS_ERROR(nvssVideoGetOutputStream(stitcher, NVSTITCH_EYE_MONO, &outStreamID));
		// Synchronize CUDA before snapping end time 
		cuStreamSynchronize(outStreamID);

		//unsigned char *out_stacked = nullptr;
		nvstitchImageBuffer_t output_image;
		CHECK_NVSS_ERROR(nvssVideoGetOutputBuffer(stitcher, NVSTITCH_EYE_MONO, &output_image));

		//encode
		EncodeBuffer *pEncodeBuffer = m_EncodeBufferQueue.GetAvailable();
		if (!pEncodeBuffer)
		{
			std::cout << "Error m_EncodeBufferQueue.GetAvailable" << std::endl;
			continue;
			//return NVSTITCH_ERROR_GENERAL;
		}
		/*if (cudaMemcpy2D((void*)pEncodeBuffer->stInputBfr.pNV12devPtr, pEncodeBuffer->stInputBfr.uNV12Stride,
			output_image.dev_ptr, output_image.pitch,
			output_image.row_bytes, output_image.height,
			cudaMemcpyDeviceToDevice) != cudaSuccess)*/
		if(cuMemcpyDtoD((CUdeviceptr)pEncodeBuffer->stInputBfr.pNV12devPtr,
			            (CUdeviceptr)output_image.dev_ptr, output_image.row_bytes * output_image.height) != CUDA_SUCCESS)
		{
			std::cout << "Error copying RGBA image bitmap to CUDA buffer" << std::endl;
			continue;
			//return NVSTITCH_ERROR_GENERAL;
		}

		NVENCSTATUS nvStatus = m_pNvHWEncoder->NvEncMapInputResource(pEncodeBuffer->stInputBfr.nvRegisteredResource, &pEncodeBuffer->stInputBfr.hInputSurface);
		if (nvStatus != NV_ENC_SUCCESS)
		{
			PRINTERR("Failed to Map input buffer %p\n", pEncodeBuffer->stInputBfr.hInputSurface);
			continue;
			//return NVSTITCH_ERROR_GENERAL;
		}

		m_pNvHWEncoder->NvEncEncodeFrame(pEncodeBuffer, NULL, m_stEncoderInput.width, m_stEncoderInput.height);

		m_EncodeBufferQueue.incPending();
	}

	/*if (out_stacked == nullptr)
		out_stacked = (unsigned char *)malloc(output_image.row_bytes * output_image.height);

	if (cudaMemcpy2D(out_stacked, output_image.row_bytes,
		output_image.dev_ptr, output_image.pitch,
		output_image.row_bytes, output_image.height,
		cudaMemcpyDeviceToHost) != cudaSuccess)
	{
		std::cout << "Error copying output stacked panorama from CUDA buffer" << std::endl;
		return NVSTITCH_ERROR_GENERAL;
	}

	char filename[256];
	sprintf(filename, "./img/%d.bmp", imgcnt++);
	putRgbaImage(filename, out_stacked, output_image.width, output_image.height);

	free(out_stacked);*/

	//return NVSTITCH_SUCCESS;
}

void app::encode_thread()
{
	while (!bEncodeThreadExit)
	{
		//if (!m_EncodeBufferQueue.isAllPending())
		//	continue;
		EncodeBuffer *pEncodeBuffer = m_EncodeBufferQueue.GetPending();
		if (!pEncodeBuffer)
		{
			usleep(10*1000);
			continue;
		}
		m_pNvHWEncoder->ProcessOutput(pEncodeBuffer);
		// UnMap the input buffer after frame done
		if (pEncodeBuffer->stInputBfr.hInputSurface)
		{
			NVENCSTATUS nvStatus = m_pNvHWEncoder->NvEncUnmapInputResource(pEncodeBuffer->stInputBfr.hInputSurface);
			pEncodeBuffer->stInputBfr.hInputSurface = NULL;
		}
		m_EncodeBufferQueue.decPending();
	}
}

nvstitchResult app::feedVideoData(int index, void* pData, int size, int64_t timestamp)
{
	EnterCriticalSection(&vCriticalSection_);
	//m_params->payloads[index].payload.frame.ptr = pData;
	//m_params->payloads[index].payload.frame.size = size;
	//m_params->payloads[index].payload.frame.timestamp = timestamp; // timestamp;
	m_params->payloads[index].payload.buffer.ptr = pData;
	m_params->payloads[index].payload.buffer.pitch = size;
	m_params->payloads[index].payload.buffer.timestamp = timestamp;

	
	//RETURN_NVSTITCH_ERROR(nvstitchFeedStitcherVideo(0, stitcher, index, &m_params->payloads[index], true)); //true

	LeaveCriticalSection(&vCriticalSection_);

	return NVSTITCH_SUCCESS;
}


#define __cu(a) do { CUresult  ret; if ((ret = (a)) != CUDA_SUCCESS) { fprintf(stderr, "%s has returned CUDA error %d\n", #a, ret); return NV_ENC_ERR_GENERIC;}} while(0)

#define BITSTREAM_BUFFER_SIZE 2*1024*1024
int app::initNVEncode()
{
	NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
	bool bError = false;
	m_uEncodeBufferCount = 0;
	memset(&m_stEncoderInput, 0, sizeof(m_stEncoderInput));
	memset(&m_stEOSOutputBfr, 0, sizeof(m_stEOSOutputBfr));
	memset(&m_stEncodeBuffer, 0, sizeof(m_stEncodeBuffer));

	EncodeConfig encodeConfig;
	memset(&encodeConfig, 0, sizeof(EncodeConfig));

	encodeConfig.endFrameIdx = INT_MAX;
	encodeConfig.bitrate = 10000000;
	encodeConfig.rcMode = NV_ENC_PARAMS_RC_CONSTQP;
	encodeConfig.gopLength = 50;// NVENC_INFINITE_GOPLENGTH; lihengz
	encodeConfig.deviceType = NV_ENC_CUDA;
	encodeConfig.codec = NV_ENC_H264;
	encodeConfig.fps = 30;
	encodeConfig.qp = 28;
	encodeConfig.i_quant_factor = DEFAULT_I_QFACTOR;
	encodeConfig.b_quant_factor = DEFAULT_B_QFACTOR;
	encodeConfig.i_quant_offset = DEFAULT_I_QOFFSET;
	encodeConfig.b_quant_offset = DEFAULT_B_QOFFSET;
	encodeConfig.presetGUID = NV_ENC_PRESET_DEFAULT_GUID;
	encodeConfig.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
	encodeConfig.inputFormat = NV_ENC_BUFFER_FORMAT_ARGB;
	encodeConfig.maxWidth = encodeConfig.width = m_params->stitcher_properties.output_payloads->image_size.x;
	encodeConfig.maxHeight = encodeConfig.height = m_params->stitcher_properties.output_payloads->image_size.y;

	encodeConfig.fOutput = fopen("./test.264", "wb"); //todel
	encodeConfig.cbfunc = onStitchedOutput;
	encodeConfig.cbcontext = this;

	m_pNvHWEncoder = new CNvHWEncoder;
	nvStatus = m_pNvHWEncoder->Initialize((void*)oContext_, NV_ENC_DEVICE_TYPE_CUDA);
	if (nvStatus != NV_ENC_SUCCESS)
		return 1;

	encodeConfig.presetGUID = m_pNvHWEncoder->GetPresetGUID(encodeConfig.encoderPreset, encodeConfig.codec);

	nvStatus = m_pNvHWEncoder->CreateEncoder(&encodeConfig);
	if (nvStatus != NV_ENC_SUCCESS)
		return 1;

	//m_uEncodeBufferCount = encodeConfig.numB + 4;
	int numMBs = ((encodeConfig.maxHeight + 15) >> 4) * ((encodeConfig.maxWidth + 15) >> 4);
	int NumIOBuffers;
	if (numMBs >= 32768) //4kx2k
		NumIOBuffers = MAX_ENCODE_QUEUE / 8;
	else if (numMBs >= 16384) // 2kx2k
		NumIOBuffers = MAX_ENCODE_QUEUE / 4;
	else if (numMBs >= 8160) // 1920x1080
		NumIOBuffers = MAX_ENCODE_QUEUE / 2;
	else
		NumIOBuffers = MAX_ENCODE_QUEUE;
	m_uEncodeBufferCount = NumIOBuffers;

	m_stEncoderInput.enableAsyncMode = 0;// encodeConfig.enableAsyncMode;
	m_stEncoderInput.width = encodeConfig.width;
	m_stEncoderInput.height = encodeConfig.height;

	nvStatus = AllocateIOBuffers(encodeConfig.width, encodeConfig.height, encodeConfig.inputFormat);
	if (nvStatus != NV_ENC_SUCCESS)
		return 1;

	return 0;
}

NVENCSTATUS app::AllocateIOBuffers(uint32_t uInputWidth, uint32_t uInputHeight, NV_ENC_BUFFER_FORMAT inputFormat)
{
	NVENCSTATUS nvStatus = NV_ENC_SUCCESS;

	m_EncodeBufferQueue.Initialize(m_stEncodeBuffer, m_uEncodeBufferCount);

	CCudaAutoLock cuLock(oContext_);

	/*__cu(cuMemAlloc(&m_ChromaDevPtr[0], uInputWidth*uInputHeight / 4));
	__cu(cuMemAlloc(&m_ChromaDevPtr[1], uInputWidth*uInputHeight / 4));

	__cu(cuMemAllocHost((void **)&m_yuv[0], uInputWidth*uInputHeight));
	__cu(cuMemAllocHost((void **)&m_yuv[1], uInputWidth*uInputHeight / 4));
	__cu(cuMemAllocHost((void **)&m_yuv[2], uInputWidth*uInputHeight / 4));*/

	for (uint32_t i = 0; i < m_uEncodeBufferCount; i++)
	{
		__cu(cuMemAllocPitch(&m_stEncodeBuffer[i].stInputBfr.pNV12devPtr, (size_t *)&m_stEncodeBuffer[i].stInputBfr.uNV12Stride, uInputWidth*4, uInputHeight, 16));

		nvStatus = m_pNvHWEncoder->NvEncRegisterResource(NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR, (void*)m_stEncodeBuffer[i].stInputBfr.pNV12devPtr,
			uInputWidth, uInputHeight, m_stEncodeBuffer[i].stInputBfr.uNV12Stride, &m_stEncodeBuffer[i].stInputBfr.nvRegisteredResource, inputFormat);
		if (nvStatus != NV_ENC_SUCCESS)
			return nvStatus;

		m_stEncodeBuffer[i].stInputBfr.bufferFmt = inputFormat; // NV_ENC_BUFFER_FORMAT_NV12_PL;
		m_stEncodeBuffer[i].stInputBfr.dwWidth = uInputWidth;
		m_stEncodeBuffer[i].stInputBfr.dwHeight = uInputHeight;

		nvStatus = m_pNvHWEncoder->NvEncCreateBitstreamBuffer(BITSTREAM_BUFFER_SIZE, &m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer);
		if (nvStatus != NV_ENC_SUCCESS)
			return nvStatus;
		m_stEncodeBuffer[i].stOutputBfr.dwBitstreamBufferSize = BITSTREAM_BUFFER_SIZE;

		if (m_stEncoderInput.enableAsyncMode)
		{
			nvStatus = m_pNvHWEncoder->NvEncRegisterAsyncEvent(&m_stEncodeBuffer[i].stOutputBfr.hOutputEvent);
			if (nvStatus != NV_ENC_SUCCESS)
				return nvStatus;
			m_stEncodeBuffer[i].stOutputBfr.bWaitOnEvent = true;
		}
		else
			m_stEncodeBuffer[i].stOutputBfr.hOutputEvent = NULL;
	}

	m_stEOSOutputBfr.bEOSFlag = TRUE;
	if (m_stEncoderInput.enableAsyncMode)
	{
		nvStatus = m_pNvHWEncoder->NvEncRegisterAsyncEvent(&m_stEOSOutputBfr.hOutputEvent);
		if (nvStatus != NV_ENC_SUCCESS)
			return nvStatus;
	}
	else
		m_stEOSOutputBfr.hOutputEvent = NULL;

	return NV_ENC_SUCCESS;
}

NVENCSTATUS app::ReleaseIOBuffers()
{
	NVENCSTATUS nvStatus = NV_ENC_SUCCESS;

	CCudaAutoLock cuLock(oContext_);

	/*for (int i = 0; i < 2; i++)
	{
		if (m_ChromaDevPtr[i])
		{
			cuMemFree(m_ChromaDevPtr[i]);
		}
	}*/

	for (uint32_t i = 0; i < m_uEncodeBufferCount; i++)
	{
		nvStatus = m_pNvHWEncoder->NvEncUnregisterResource(m_stEncodeBuffer[i].stInputBfr.nvRegisteredResource);
		if (nvStatus != NV_ENC_SUCCESS)
			return nvStatus;

		cuMemFree(m_stEncodeBuffer[i].stInputBfr.pNV12devPtr);

		m_pNvHWEncoder->NvEncDestroyBitstreamBuffer(m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer);
		m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer = NULL;

		if (m_stEncoderInput.enableAsyncMode)
		{
			m_pNvHWEncoder->NvEncUnregisterAsyncEvent(m_stEncodeBuffer[i].stOutputBfr.hOutputEvent);
			nvCloseFile(m_stEncodeBuffer[i].stOutputBfr.hOutputEvent);
			m_stEncodeBuffer[i].stOutputBfr.hOutputEvent = NULL;
		}
	}

	if (m_stEOSOutputBfr.hOutputEvent)
	{
		if (m_stEncoderInput.enableAsyncMode)
		{
			m_pNvHWEncoder->NvEncUnregisterAsyncEvent(m_stEOSOutputBfr.hOutputEvent);
			nvCloseFile(m_stEOSOutputBfr.hOutputEvent);
			m_stEOSOutputBfr.hOutputEvent = NULL;
		}
	}

	return NV_ENC_SUCCESS;
}

//-------------------------------audio aac-------------------------------------------------------------------------
int app::initAACEncode()
{
	if (aacEncOpen(&m_aacEncHandle, 0, m_params->stitcher_properties.audio_output_format->channels) != AACENC_OK) {
		printf("Unable to open fdkaac encoder\n");
		return -1;
	}

	if (aacEncoder_SetParam(m_aacEncHandle, AACENC_AOT, 2) != AACENC_OK) {  //aac lc
		printf("Unable to set the AOT\n");
		return -1;
	}

	if (aacEncoder_SetParam(m_aacEncHandle, AACENC_SAMPLERATE, m_params->stitcher_properties.audio_output_format->sampleRate) != AACENC_OK) {
		printf("Unable to set the AOT\n");
		return -1;
	}
	if (aacEncoder_SetParam(m_aacEncHandle, AACENC_CHANNELMODE, MODE_2) != AACENC_OK) {  //2 channle
		printf("Unable to set the channel mode\n");
		return -1;
	}
	if (aacEncoder_SetParam(m_aacEncHandle, AACENC_BITRATE, m_params->stitcher_properties.audio_output_format->bitRate) != AACENC_OK) {
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

nvstitchResult app::feedAudioData(int index, void* pData, int size, int64_t timestamp)
{
	if (!m_params->audio_flag || nvsfContext_==NULL)
		return NVSTITCH_SUCCESS;

	EnterCriticalSection(&aCriticalSection_);
	float* dataPtr = (float*)pData;
	NVSF_CHECK(nvsfInputAddData(nvsfContext_, inputHandles_[index], &dataPtr, size/sizeof(float), 0));
	updateAudioFrame_[index]++;
	LeaveCriticalSection(&aCriticalSection_);

	return NVSTITCH_SUCCESS;
}


void app::stitch_audio_thread()
{
	while (!bStitchAudioThreadExit)
	{
		// Pull all valid data out of nvsf 
		uint64_t outtimestamp; // Timestamps are not used currently
/*		int framesum = 0;
		for (int i = 0; i < m_params->rig_properties.num_cameras; i++)
		{
			if (updateAudioFrame_[i] > 0)
				framesum++;
		}
		if (framesum == m_params->rig_properties.num_cameras)
		{
*/			if (NVSTITCH_SUCCESS == nvsfProcess(nvsfContext_, outBuffers_, &outtimestamp))
			{
				int i = 0;
				for (uint32_t s = 0; s < 1024; s++)
				{
					for (uint32_t ch = 0; ch < 2; ch++)
					{
						float sample32 = outBuffers_[ch][s];
						// clip
						if (sample32 > 1.0f)
							sample32 = 1.0f;
						if (sample32 < -1.0f)
							sample32 = -1.0f;
						outPcmBuffer[i++] = (short)(sample32 * 32767);
					}
				}
				encodeAAC(outPcmBuffer, 1024 * 4, outtimestamp);
			}
/*			for (int i = 0; i < m_params->rig_properties.num_cameras; i++)
			{
				updateAudioFrame_[i]--;
			}

		}*/
	}
}

nvstitchResult app::encodeAAC(void* pData, int size, int64_t timestamp)
{
	if (!m_params->audio_flag)
		return NVSTITCH_SUCCESS;

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
	in_buf.bufs = &pData;
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
		return NVSTITCH_ERROR_GENERAL;
	}
	if (out_args.numOutBytes == 0)
		return NVSTITCH_ERROR_GENERAL;
	//fwrite(outbuf, 1, out_args.numOutBytes, out);
	onAudioStitchedOutput(m_aacOutbuf, out_args.numOutBytes, timestamp);

	return NVSTITCH_SUCCESS;
}

int app::onAudioStitchedOutput(unsigned char* buffer, int bufsize, int64_t timestamp)
{
	
	if (buffer && bufsize>0)
	{
		if (rtmp_) //&& srs_write_video_
		{
			EnterCriticalSection(&rtmpCriticalSection_);
			int ret = srs_audio_write_raw_frame(rtmp_, 10, 3, 1, 1, (char *)buffer,
				bufsize, msecond() - startMS); 
			if (ret != 0) {
				srs_human_trace("send audio raw data failed. ret=%d", ret);
				//goto rtmp_destroy;
			}
			LeaveCriticalSection(&rtmpCriticalSection_);
		}

		//if (mp4AudioTrack_ && m_params->record_flag)
		//{
		//	MP4WriteSample(mp4fileHandle_, mp4AudioTrack_, (const uint8_t *)out_audio_payload->payload.buffer.ptr, out_audio_payload->payload.buffer.size, MP4_INVALID_DURATION, 0, 1);
		//}
		if (flvHandle_)
		{
			flv_write_audio_packet(flvHandle_, (uint8_t *)buffer+7, bufsize-7, msecond() - startMS); //timestamp
		}
	}
	return 0;
}
