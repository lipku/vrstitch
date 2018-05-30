set(SOURCE_FILES 
    app.cpp
    main.cpp
    srs_librtmp.cpp
    flvenc.cpp
    g711.c
    RTSPProtocol.cpp
    VideoSource.cpp
    FrameQueue.cpp
    VideoDecoder.cpp
    VideoParser.cpp
    cudaProcessFrame.cpp
    thirdparty/cuvid/src/cudaModuleMgr.cpp
    thirdparty/cuvid/src/NvHWEncoder.cpp
    thirdparty/cuvid/src/dynlink_cuda.cpp
    thirdparty/cuvid/src/dynlink_nvcuvid.cpp)

set(HEADER_FILES
    app.h
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

add_executable(nvstitch_sample ${SOURCE_FILES} ${HEADER_FILES})
target_include_directories(nvstitch_sample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} thirdparty/cuvid/inc)
target_link_libraries(nvstitch_sample PRIVATE nvstitch common_sample common_util OpenCV)

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
