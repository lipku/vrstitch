set(SOURCE_FILES 
    srs_librtmp.cpp
    flvenc.cpp
    g711.cpp
    RTSPProtocol.cpp
    VideoSource.cpp
    FrameQueue.cpp
    VideoDecoder.cpp
    VideoParser.cpp
    cudaProcessFrame.cpp
    third_party/cuvid/src/cudaModuleMgr.cpp
    third_party/cuvid/src/NvHWEncoder.cpp
    third_party/cuvid/src/dynlink_cuda.cpp
    third_party/cuvid/src/dynlink_nvcuvid.cpp
    audiocap.cpp
    app.cpp
    main.cpp)

set(HEADER_FILES
    app.h
    audiocap.h
    srs_librtmp.h
    flv.h
    g711.h
    RTSPProtocol.h
    VideoSource.h
    FrameQueue.h
    VideoDecoder.h
    VideoParser.h
    cudaProcessFrame.h)

# Set Visual Studio source filters
source_group("Source Files" FILES ${SOURCE_FILES})
source_group("Header Files" FILES ${HEADER_FILES})

set(INCLUDE_DIRS
	third_party/cuvid/inc
	/usr/local/include/BasicUsageEnvironment
	/usr/local/include/groupsock
	/usr/local/include/UsageEnvironment
	/usr/local/include/liveMedia)

add_executable(nvstitch_sample ${SOURCE_FILES} ${HEADER_FILES})
target_include_directories(nvstitch_sample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${INCLUDE_DIRS})
target_link_libraries(nvstitch_sample PRIVATE nvstitch nvss_video nvsf_lowlevel common_sample common_util OpenCV CUDA fdk-aac liveMedia BasicUsageEnvironment UsageEnvironment groupsock swresample avutil pthread dl)

set_target_properties(nvstitch_sample PROPERTIES FOLDER SampleApps)

# Copy executables into package folder
if(INSTALL_SDK)
    install(TARGETS nvstitch_sample RUNTIME DESTINATION ./samples/nvstitch_sample)

elseif(INSTALL_FLAT)
    install(TARGETS nvstitch_sample RUNTIME DESTINATION .)
if(WIN32)
    install(FILES $<TARGET_PDB_FILE:nvstitch_sample> DESTINATION . CONFIGURATIONS Debug)
endif()
endif()
