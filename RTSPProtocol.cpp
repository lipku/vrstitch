
//#include <process.h> 
#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include <GroupsockHelper.hh>

#include "RTSPProtocol.h"

// Forward function definitions:

// RTSP 'response handlers':
void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString);
void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString);
void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString);

// Other event handler functions:
void subsessionAfterPlaying(void* clientData); // called when a stream's subsession (e.g., audio or video substream) ends
void subsessionByeHandler(void* clientData); // called when a RTCP "BYE" is received for a subsession
void streamTimerHandler(void* clientData);
  // called at the end of a stream's expected duration (if the stream has not already signaled its end using a RTCP "BYE")

// The main streaming routine (for each "rtsp://" URL):
RTSPClient* openURL(UsageEnvironment& env, char const* progName, char const* rtspURL);

// Used to iterate through each stream's 'subsessions', setting up each one:
void setupNextSubsession(RTSPClient* rtspClient);

// Used to shut down and close a stream (including its "RTSPClient" object):
void shutdownStream(RTSPClient* rtspClient, int exitCode = 1);

// A function that outputs a string that identifies each stream (for debugging output).  Modify this if you wish:
UsageEnvironment& operator<<(UsageEnvironment& env, const RTSPClient& rtspClient) {
  return env << "[URL:\"" << rtspClient.url() << "\"]: ";
}

// A function that outputs a string that identifies each subsession (for debugging output).  Modify this if you wish:
UsageEnvironment& operator<<(UsageEnvironment& env, const MediaSubsession& subsession) {
  return env << subsession.mediumName() << "/" << subsession.codecName();
}

void usage(UsageEnvironment& env, char const* progName) {
  env << "Usage: " << progName << " <rtsp-url-1> ... <rtsp-url-N>\n";
  env << "\t(where each <rtsp-url-i> is a \"rtsp://\" URL)\n";
}

// Define a class to hold per-stream state that we maintain throughout each stream's lifetime:

class StreamClientState {
public:
  StreamClientState();
  virtual ~StreamClientState();

public:
  MediaSubsessionIterator* iter;
  MediaSession* session;
  MediaSubsession* subsession;
  TaskToken streamTimerTask;
  double duration;
};

// If you're streaming just a single stream (i.e., just from a single URL, once), then you can define and use just a single
// "StreamClientState" structure, as a global variable in your application.  However, because - in this demo application - we're
// showing how to play multiple streams, concurrently, we can't do that.  Instead, we have to have a separate "StreamClientState"
// struture for each "RTSPClient".  To do this, we subclass "RTSPClient", and add a "StreamClientState" field to the subclass:

class ourRTSPClient: public RTSPClient {
public:
  static ourRTSPClient* createNew(UsageEnvironment& env, char const* rtspURL,
				  int verbosityLevel,
				  char const* applicationName,
				  portNumBits tunnelOverHTTPPortNum,int socketNumToServer,
				  StreamCallback streamCallback,void *pUser);

protected:
  ourRTSPClient(UsageEnvironment& env, char const* rtspURL,
		int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum,int socketNumToServer,
		StreamCallback streamCallback,void *pUser);
    // called only by createNew();
  virtual ~ourRTSPClient();

public:
  StreamClientState scs;
  StreamCallback funcStreamCallback;
  void *pCallbackContext;

  TaskToken fLivenessCommandTask;
  int fRecvMediaNum;
//  char watchEvent;
};

// Define a data sink (a subclass of "MediaSink") to receive the data for each subsession (i.e., each audio or video 'substream').
// In practice, this might be a class (or a chain of classes) that decodes and then renders the incoming audio or video.
// Or it might be a "FileSink", for outputting the received data into a file (as is done by the "openRTSP" application).
// In this example code, however, we define a simple 'dummy' sink that receives incoming data, but does nothing with it.

class DummySink: public MediaSink {
public:
  static DummySink* createNew(UsageEnvironment& env,
			      MediaSubsession& subsession, // identifies the kind of data that's being received
			      RTSPClient *rtspClient); // identifies the stream itself (optional)

private:
  DummySink(UsageEnvironment& env, MediaSubsession& subsession, RTSPClient *rtspClient);
    // called only by "createNew()"
  virtual ~DummySink();

  static void afterGettingFrame(void* clientData, unsigned frameSize,
                                unsigned numTruncatedBytes,
				struct timeval presentationTime,
                                unsigned durationInMicroseconds);
  void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
			 struct timeval presentationTime, unsigned durationInMicroseconds);

private:
  // redefined virtual functions:
  virtual Boolean continuePlaying();

private:
  u_int8_t* fReceiveBuffer;
  int fBufferSize;
  MediaSubsession& fSubsession;
  RTSPClient *fRtspClient;

  u_int8_t* fNalBuffer; //lihengz
  int fNalSize;
};

#define RTSP_CLIENT_VERBOSITY_LEVEL 1 // by default, print verbose output from each "RTSPClient"

/***************************modify by lihengz 2012-4-3*****************************************************************/
//#include "e/e.h"

/*
using namespace e::sys;

class RtspRunnable : public IRunnable {

public:

	ourRTSPClient *m_rtspclient;

	RtspRunnable(ourRTSPClient *rtspclient) {
		m_rtspclient=rtspclient;
	}

private:
	virtual void run() {
		assert(IThread::current());
		UsageEnvironment& env = m_rtspclient->envir();
		m_rtspclient->sendDescribeCommand(continueAfterDESCRIBE);
		E_DBG1_("rtsp发送describe命令 "<<m_rtspclient->url());
		env.taskScheduler().doEventLoop(&(m_rtspclient->watchEvent)); // does not return
		E_DBG1_("rtsp断开连接... "<<m_rtspclient->url());
		shutdownStream(m_rtspclient);

	}
	virtual void onJoin() {
	}
};


void* NewRtspClient(const char* rtspUrl,StreamCallback funcStreamCallback,void *pCallbackContext) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);


  // There are argc-1 URLs: argv[1] through argv[argc-1].  Open and start streaming each one:
  
//  ourRTSPClient* rtspClient=(ourRTSPClient *)openURL(*env, "",rtspUrl);
  ourRTSPClient* rtspClient = ourRTSPClient::createNew(*env, rtspUrl, RTSP_CLIENT_VERBOSITY_LEVEL, "");
  if (rtspClient == NULL) {
	  *env << "Failed to create a RTSP client for URL \"" << rtspUrl << "\": " << env->getResultMsg() << "\n";
	  return NULL;
  }

  // All subsequent activity takes place within the event loop:
  if(rtspClient!=NULL)
  {
		rtspClient->funcStreamCallback=funcStreamCallback;
		rtspClient->pCallbackContext=pCallbackContext;
		rtspClient->watchEvent=0;
		IRunnablePtr runnable(new RtspRunnable(rtspClient)); //新建一个线程连接视频
		IThreadPtr th = IThread::New(runnable);
//		_beginthreadex(NULL,0,rtspclientthrd,rtspClient,0,NULL);
  }

  return rtspClient; // We never actually get to this line; this is only to prevent a possible compiler warning
}

/*
int RunRtspClient(void *rtspClient)
{
	_beginthreadex(NULL,0,rtspclientthrd,rtspClient,0,NULL);
	return 0;
}*/

static void TaskCloseStream( void *p_private )
{
	RTSPClient *rtspClient = (RTSPClient*)p_private;

	shutdownStream(rtspClient);
	Medium::close(rtspClient);

}

int CloseRtspClient(RTSPClient *rtspClient)
{
//	RTSPClient* pRtspClient=(RTSPClient *)rtspClient;
//	if(rtspClient)
//		((ourRTSPClient*)rtspClient)->watchEvent=0xff;

//	rtspClient->envir().taskScheduler().scheduleDelayedTask( 100000, TaskCloseStream, rtspClient ); //100ms
	shutdownStream(rtspClient);
	Medium::close(rtspClient);

	return 0;
}


void scheduleLivenessCommand(RTSPClient* rtspClient);

static void continueAfterOPTIONS(RTSPClient* rtspClient, int resultCode, char* resultString) {
/*	Boolean serverSupportsGetParameter = False;
	if (resultCode == 0) {
		// Note whether the server told us that it supports the "GET_PARAMETER" command:
		serverSupportsGetParameter = RTSPOptionIsSupported("GET_PARAMETER", resultString);
	}*/
//	((ProxyRTSPClient*)rtspClient)->continueAfterLivenessCommand(resultCode, serverSupportsGetParameter);
	if(resultCode==0)
		scheduleLivenessCommand(rtspClient);
	delete[] resultString;
}

void sendLivenessCommand(void* clientData) {
	ourRTSPClient* rtspClient = (ourRTSPClient*)clientData;

	// Note.  By default, we do not send "GET_PARAMETER" as our 'liveness notification' command, even if the server previously
	// indicated (in its response to our earlier "OPTIONS" command) that it supported "GET_PARAMETER".  This is because
	// "GET_PARAMETER" crashes some camera servers (even though they claimed to support "GET_PARAMETER").
#ifdef SEND_GET_PARAMETER_IF_SUPPORTED
	MediaSession* sess = rtspClient->fOurServerMediaSession.fClientMediaSession;

	if (rtspClient->fServerSupportsGetParameter && rtspClient->fNumSetupsDone > 0 && sess != NULL) {
		rtspClient->sendGetParameterCommand(*sess, ::continueAfterGET_PARAMETER, "", rtspClient->auth());
	} else {
#endif
		rtspClient->sendOptionsCommand(::continueAfterOPTIONS);
#ifdef SEND_GET_PARAMETER_IF_SUPPORTED
	}
#endif
}

void scheduleLivenessCommand(RTSPClient* rtspClient) {
	// Delay a random time before sending another 'liveness' command.
	unsigned delayMax = 0;//rtspClient->sessionTimeoutParameter(); // if the server specified a maximum time between 'liveness' probes, then use that
	if (delayMax == 0) {
		delayMax = 40; //60
	}

	// Choose a random time from [delayMax/2,delayMax) seconds:
	unsigned const dM5 = delayMax*500000;
	unsigned uSecondsToDelay = dM5 + (dM5*our_random())%dM5;
	((ourRTSPClient*)rtspClient)->fLivenessCommandTask = rtspClient->envir().taskScheduler().scheduleDelayedTask(uSecondsToDelay, sendLivenessCommand, rtspClient);
}

/*
void TaskMonitorData(void* clientData) {
	ourRTSPClient* rtspClient = (ourRTSPClient*)clientData;
	if (rtspClient->fRecvMediaNum <= 0)
	{
		printf("Failed to recv media data in 10s for URL %s\n", rtspClient->url() );
		shutdownStream(rtspClient);
		rtspClient->sendDescribeCommand(continueAfterDESCRIBE);
	}

	rtspClient->fRecvMediaNum = 0;

	rtspClient->envir().taskScheduler().scheduleDelayedTask(10000 * 1000, TaskMonitorData, rtspClient); //10 second
}*/


/********************************************************************************************************************/

//static unsigned rtspClientCount = 0; // Counts how many streams (i.e., "RTSPClient"s) are currently in use.

RTSPClient* openURL(UsageEnvironment& env, char const* rtspURL,int socketNumToServer,StreamCallback funcStreamCallback,void *pCallbackContext) {
  // Begin by creating a "RTSPClient" object.  Note that there is a separate "RTSPClient" object for each stream that we wish
  // to receive (even if more than stream uses the same "rtsp://" URL).
//  RTSPClient::responseBufferSize = 500000;

  RTSPClient* rtspClient = ourRTSPClient::createNew(env, rtspURL, RTSP_CLIENT_VERBOSITY_LEVEL, "rtspnvr",0,
													socketNumToServer,funcStreamCallback,pCallbackContext);
  if (rtspClient == NULL) {
    env << "Failed to create a RTSP client for URL \"" << rtspURL << "\": " << env.getResultMsg() << "\n";
    return NULL;
  }

//  ++rtspClientCount;

  // Next, send a RTSP "DESCRIBE" command, to get a SDP description for the stream.
  // Note that this command - like all RTSP commands - is sent asynchronously; we do not block, waiting for a response.
  // Instead, the following function call returns immediately, and we handle the RTSP response later, from within the event loop:
  rtspClient->sendDescribeCommand(continueAfterDESCRIBE);

  //rtspClient->envir().taskScheduler().scheduleDelayedTask(10000*1000, TaskMonitorData, rtspClient); //10 second
  //E_DBG1_("open rtsp:"<<rtspURL);
  
  return rtspClient;
}


// Implementation of the RTSP 'response handlers':

void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString) {
  do {
    UsageEnvironment& env = rtspClient->envir(); // alias
    StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

    if (resultCode != 0) {
      env << *rtspClient << "Failed to get a SDP description: " << resultString << "\n";
	  delete[] resultString;
      break;
    }

    char* sdpDescription = resultString;
    env << *rtspClient << "Got a SDP description:\n" << sdpDescription << "\n";

//	scheduleLivenessCommand(rtspClient); //add by lihengz 2013-8-30

    // Create a media session object from this SDP description:
    scs.session = MediaSession::createNew(env, sdpDescription);
    delete[] sdpDescription; // because we don't need it anymore
    if (scs.session == NULL) {
      env << *rtspClient << "Failed to create a MediaSession object from the SDP description: " << env.getResultMsg() << "\n";
      break;
    } else if (!scs.session->hasSubsessions()) {
      env << *rtspClient << "This session has no media subsessions (i.e., no \"m=\" lines)\n";
      break;
    }

    // Then, create and set up our data source objects for the session.  We do this by iterating over the session's 'subsessions',
    // calling "MediaSubsession::initiate()", and then sending a RTSP "SETUP" command, on each one.
    // (Each 'subsession' will have its own data source.)
    scs.iter = new MediaSubsessionIterator(*scs.session);
    setupNextSubsession(rtspClient);

    return;
  } while (0);

  // An unrecoverable error occurred with this stream.
  shutdownStream(rtspClient);
//  CloseRtspClient(rtspClient);
}
#define REQUEST_STREAMING_OVER_TCP False //False //True

void setupNextSubsession(RTSPClient* rtspClient) {
  UsageEnvironment& env = rtspClient->envir(); // alias
  StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias
  
  scs.subsession = scs.iter->next();
  if (scs.subsession != NULL) {
    if (!scs.subsession->initiate()) {
      env << *rtspClient << "Failed to initiate the \"" << *scs.subsession << "\" subsession: " << env.getResultMsg() << "\n";
      setupNextSubsession(rtspClient); // give up on this subsession; go to the next one
    } else {
      env << *rtspClient << "Initiated the \"" << *scs.subsession
	  << "\" subsession (client ports " << scs.subsession->clientPortNum() << "-" << scs.subsession->clientPortNum()+1 << ")\n";

	  if( scs.subsession->rtpSource() != NULL )
	  {
		  int socketNum = scs.subsession->rtpSource()->RTPgs()->socketNum();

		  /* Increase the buffer size */
		  if(  !strcmp( scs.subsession->mediumName(), "video" ) )
			  increaseReceiveBufferTo( env, socketNum, 1000000 ); //default 50k

		  unsigned const thresh = 200000; /* RTP reorder threshold .2 second (default .1) */
		  /* Increase the RTP reorder timebuffer just a bit */
		  scs.subsession->rtpSource()->setPacketReorderingThresholdTime(thresh);
	  }
      // Continue setting up this subsession, by sending a RTSP "SETUP" command:
      rtspClient->sendSetupCommand(*scs.subsession, continueAfterSETUP, False, REQUEST_STREAMING_OVER_TCP);
    }
    return;
  }

  // We've finished setting up all of the subsessions.  Now, send a RTSP "PLAY" command to start the streaming:
  if (scs.session->absStartTime() != NULL) {
    // Special case: The stream is indexed by 'absolute' time, so send an appropriate "PLAY" command:
    rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY, scs.session->absStartTime(), scs.session->absEndTime());
  } else {
    scs.duration = scs.session->playEndTime() - scs.session->playStartTime();
    rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY);
  }

}

void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString) {
  do {
    UsageEnvironment& env = rtspClient->envir(); // alias
    StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

    if (resultCode != 0) {
      env << *rtspClient << "Failed to set up the \"" << *scs.subsession << "\" subsession: " << resultString << "\n";
      break;
    }

    env << *rtspClient << "Set up the \"" << *scs.subsession
	<< "\" subsession (client ports " << scs.subsession->clientPortNum() << "-" << scs.subsession->clientPortNum()+1 << ")\n";

    // Having successfully setup the subsession, create a data sink for it, and call "startPlaying()" on it.
    // (This will prepare the data sink to receive data; the actual flow of data from the client won't start happening until later,
    // after we've sent a RTSP "PLAY" command.)

    scs.subsession->sink = DummySink::createNew(env, *scs.subsession, rtspClient/*->url()*/);
      // perhaps use your own custom "MediaSink" subclass instead
    if (scs.subsession->sink == NULL) {
      env << *rtspClient << "Failed to create a data sink for the \"" << *scs.subsession
	  << "\" subsession: " << env.getResultMsg() << "\n";
      break;
    }

    env << *rtspClient << "Created a data sink for the \"" << *scs.subsession << "\" subsession\n";
    scs.subsession->miscPtr = rtspClient; // a hack to let subsession handle functions get the "RTSPClient" from the subsession 
    scs.subsession->sink->startPlaying(*(scs.subsession->readSource()),
				       subsessionAfterPlaying, scs.subsession);
    // Also set a handler to be called if a RTCP "BYE" arrives for this subsession:
    if (scs.subsession->rtcpInstance() != NULL) {
      scs.subsession->rtcpInstance()->setByeHandler(subsessionByeHandler, scs.subsession);
    }
  } while (0);
  delete[] resultString;

  // Set up the next subsession, if any:
  setupNextSubsession(rtspClient);
}

void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString) {
  Boolean success = False;
  do {
    UsageEnvironment& env = rtspClient->envir(); // alias
    StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

    if (resultCode != 0) {
      env << *rtspClient << "Failed to start playing session: " << resultString << "\n";
      break;
    }

/*	MediaSubsessionIterator iter(*scs.session);
	MediaSubsession* subsession;
	while((subsession = iter.next())!=NULL) //add by lihengz 2013-8-25
	{
		subsession->sink->startPlaying(*(subsession->readSource()),subsessionAfterPlaying, subsession);
	}*/

    // Set a timer to be handled at the end of the stream's expected duration (if the stream does not already signal its end
    // using a RTCP "BYE").  This is optional.  If, instead, you want to keep the stream active - e.g., so you can later
    // 'seek' back within it and do another RTSP "PLAY" - then you can omit this code.
    // (Alternatively, if you don't want to receive the entire stream, you could set this timer for some shorter value.)
    if (scs.duration > 0) {
      unsigned const delaySlop = 2; // number of seconds extra to delay, after the stream's expected duration.  (This is optional.)
      scs.duration += delaySlop;
      unsigned uSecsToDelay = (unsigned)(scs.duration*1000000);
      scs.streamTimerTask = env.taskScheduler().scheduleDelayedTask(uSecsToDelay, (TaskFunc*)streamTimerHandler, rtspClient);
    }

    env << *rtspClient << "Started playing session";
    if (scs.duration > 0) {
      env << " (for up to " << scs.duration << " seconds)";
    }
    env << "...\n";

    success = True;
  } while (0);
  delete[] resultString;

  if (!success) {
    // An unrecoverable error occurred with this stream.
	shutdownStream(rtspClient);
//  	CloseRtspClient(rtspClient);
  }
}


// Implementation of the other event handlers:

void subsessionAfterPlaying(void* clientData) {
  MediaSubsession* subsession = (MediaSubsession*)clientData;
  RTSPClient* rtspClient = (RTSPClient*)(subsession->miscPtr);

  // Begin by closing this subsession's stream:
  Medium::close(subsession->sink);
  subsession->sink = NULL;

  // Next, check whether *all* subsessions' streams have now been closed:
  MediaSession& session = subsession->parentSession();
  MediaSubsessionIterator iter(session);
  while ((subsession = iter.next()) != NULL) {
    if (subsession->sink != NULL) return; // this subsession is still active
  }

  // All subsessions' streams have now been closed, so shutdown the client:
  shutdownStream(rtspClient);
//  CloseRtspClient(rtspClient);
}

void subsessionByeHandler(void* clientData) {
  MediaSubsession* subsession = (MediaSubsession*)clientData;
  RTSPClient* rtspClient = (RTSPClient*)subsession->miscPtr;
  UsageEnvironment& env = rtspClient->envir(); // alias

  env << *rtspClient << "Received RTCP \"BYE\" on \"" << *subsession << "\" subsession\n";

  // Now act as if the subsession had closed:
  subsessionAfterPlaying(subsession);
}

void streamTimerHandler(void* clientData) {
  ourRTSPClient* rtspClient = (ourRTSPClient*)clientData;
  StreamClientState& scs = rtspClient->scs; // alias

  scs.streamTimerTask = NULL;

  // Shut down the stream:
  shutdownStream(rtspClient);
//  CloseRtspClient(rtspClient);
}

void shutdownStream(RTSPClient* rtspClient, int exitCode) {
  UsageEnvironment& env = rtspClient->envir(); // alias
  StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

  env.taskScheduler().unscheduleDelayedTask(((ourRTSPClient*)rtspClient)->fLivenessCommandTask);

  // First, check whether any subsessions have still to be closed:
  if (scs.session != NULL) { 
    Boolean someSubsessionsWereActive = False;
    MediaSubsessionIterator iter(*scs.session);
    MediaSubsession* subsession;

	while ((subsession = iter.next()) != NULL) {
		if (subsession->sink != NULL) {
			Medium::close(subsession->sink);
			subsession->sink = NULL;
			if (subsession->rtcpInstance() != NULL) {
				subsession->rtcpInstance()->setByeHandler(NULL, NULL); // in case the server sends a RTCP "BYE" while handling "TEARDOWN"
			}
			someSubsessionsWereActive = True;
		}
	}

	if (someSubsessionsWereActive) {
		// Send a RTSP "TEARDOWN" command, to tell the server to shutdown the stream.
		// Don't bother handling the response to the "TEARDOWN".
		rtspClient->sendTeardownCommand(*scs.session, NULL);
	}

	Medium::close( scs.session );

	scs.session=NULL;
  }

  env << *rtspClient << "Closing the stream.\n";
//  E_DBG1_("close rtsp:"<<rtspClient->url());
//  Medium::close(rtspClient);

//  ((ourRTSPClient*)rtspClient)->watchEvent=0xff;
  
    // Note that this will also cause this stream's "StreamClientState" structure to get reclaimed.

/*  if (--rtspClientCount == 0) {
    // The final stream has ended, so exit the application now.
    // (Of course, if you're embedding this code into your own application, you might want to comment this out.)
    exit(exitCode);
  }*/

/*   TaskScheduler* scheduler = &(env.taskScheduler());
   env.reclaim();
   delete scheduler;*/
}


// Implementation of "ourRTSPClient":

ourRTSPClient* ourRTSPClient::createNew(UsageEnvironment& env, char const* rtspURL,
										int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum,int socketNumToServer,
										StreamCallback streamCallback,void *pUser) {
  return new ourRTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum,socketNumToServer,streamCallback,pUser);
}

ourRTSPClient::ourRTSPClient(UsageEnvironment& env, char const* rtspURL,
							 int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum,int socketNumToServer,
							 StreamCallback streamCallback,void *pUser)
  : RTSPClient(env,rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum,socketNumToServer) ,
  funcStreamCallback(streamCallback),pCallbackContext(pUser),fLivenessCommandTask(NULL),fRecvMediaNum(0) {
}

ourRTSPClient::~ourRTSPClient() {
}


// Implementation of "StreamClientState":

StreamClientState::StreamClientState()
  : iter(NULL), session(NULL), subsession(NULL), streamTimerTask(NULL), duration(0.0) {
}

StreamClientState::~StreamClientState() {
  delete iter;
  if (session != NULL) {
    // We also need to delete "session", and unschedule "streamTimerTask" (if set)
    UsageEnvironment& env = session->envir(); // alias

    env.taskScheduler().unscheduleDelayedTask(streamTimerTask);
    Medium::close(session);
  }
}


// Implementation of "DummySink":

// Even though we're not going to be doing anything with the incoming data, we still need to receive it.
// Define the size of the buffer that we'll use:
#define DUMMY_SINK_RECEIVE_BUFFER_SIZE 1000000  //1000000

DummySink* DummySink::createNew(UsageEnvironment& env, MediaSubsession& subsession, RTSPClient *rtspClient) {
  return new DummySink(env, subsession, rtspClient);
}

DummySink::DummySink(UsageEnvironment& env, MediaSubsession& subsession, RTSPClient *rtspClient)
  : MediaSink(env),
    fSubsession(subsession) {
  fRtspClient = rtspClient;
  fBufferSize=DUMMY_SINK_RECEIVE_BUFFER_SIZE;
  fReceiveBuffer = new u_int8_t[DUMMY_SINK_RECEIVE_BUFFER_SIZE];

  fNalBuffer = new u_int8_t[DUMMY_SINK_RECEIVE_BUFFER_SIZE];
  fNalSize = 0;
}

DummySink::~DummySink() {
  delete[] fReceiveBuffer;

  delete[] fNalBuffer;
//  delete[] fStreamId;
}

void DummySink::afterGettingFrame(void* clientData, unsigned frameSize, unsigned numTruncatedBytes,
				  struct timeval presentationTime, unsigned durationInMicroseconds) {
  DummySink* sink = (DummySink*)clientData;
  sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
}

// If you don't want to see debugging output for each received frame, then comment out the following line:
//#define DEBUG_PRINT_EACH_RECEIVED_FRAME 1

/**
*  Add ADTS header at the beginning of each and every AAC packet.
*  This is needed as MediaCodec encoder generates a packet of raw
*  AAC data.
*
*  Note the packetLen must count in the ADTS header itself.
**/
static void addADTStoPacket(unsigned char* packet, int packetLen) {
	int profile = 2;  //AAC LC
					  //39=MediaCodecInfo.CodecProfileLevel.AACObjectELD;
	int freqIdx = 4;// 4;  //44.1KHz
	int chanCfg = 2;// 2;  //CPE
	/*int avpriv_mpeg4audio_sample_rates[] = {
            96000, 88200, 64000, 48000, 44100, 32000,
                    24000, 22050, 16000, 12000, 11025, 8000, 7350
        };
        channel_configuration: 表示声道数chanCfg
        0: Defined in AOT Specifc Config
        1: 1 channel: front-center
        2: 2 channels: front-left, front-right
        3: 3 channels: front-center, front-left, front-right
        4: 4 channels: front-center, front-left, front-right, back-center
        5: 5 channels: front-center, front-left, front-right, back-left, back-right
        6: 6 channels: front-center, front-left, front-right, back-left, back-right, LFE-channel
        7: 8 channels: front-center, front-left, front-right, side-left, side-right, back-left, back-right, LFE-channel
        8-15: Reserved
        */


					  // fill in ADTS data
	packet[0] = (unsigned char)0xFF;
	packet[1] = (unsigned char)0xF9;
	packet[2] = (unsigned char)(((profile - 1) << 6) + (freqIdx << 2) + (chanCfg >> 2));
	packet[3] = (unsigned char)(((chanCfg & 3) << 6) + (packetLen >> 11));
	packet[4] = (unsigned char)((packetLen & 0x7FF) >> 3);
	packet[5] = (unsigned char)(((packetLen & 7) << 5) + 0x1F);
	packet[6] = (unsigned char)0xFC;
}

void DummySink::afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
				  struct timeval presentationTime, unsigned /*durationInMicroseconds*/) {
  // We've just received a frame of data.  (Optionally) print out information about it:
  unsigned int i_pts = presentationTime.tv_sec*1000 + presentationTime.tv_usec/1000;
  int marker=fSubsession.rtpSource()->curPacketMarkerBit();
  ourRTSPClient *rtspClient=(ourRTSPClient*)fRtspClient;
  rtspClient->fRecvMediaNum++;

  if (!strcmp(fSubsession.codecName(), "H264"))
  {
	  //if (fReceiveBuffer[0] == 0x67 || fReceiveBuffer[0] == 0x68 || fReceiveBuffer[0] == 0x65 || fReceiveBuffer[0] == 0x61) {
		  memmove(fReceiveBuffer + 4, fReceiveBuffer, frameSize);
		  fReceiveBuffer[0] = 0x00;
		  fReceiveBuffer[1] = 0x00;
		  fReceiveBuffer[2] = 0x00;
		  fReceiveBuffer[3] = 0x01;
		  if (fReceiveBuffer[4] == 0x67 || fReceiveBuffer[4] == 0x68 || fReceiveBuffer[4] == 0x06)
		  {
			  memcpy(fNalBuffer + fNalSize, fReceiveBuffer, frameSize + 4);
			  fNalSize += frameSize + 4;
		  }
		  else if (fNalSize>0)
		  {
			  memcpy(fNalBuffer + fNalSize, fReceiveBuffer, frameSize + 4);
			  rtspClient->funcStreamCallback(fNalBuffer, fNalSize + frameSize + 4, i_pts, marker,
				  fSubsession.codecName(), rtspClient->pCallbackContext);
			  fNalSize = 0;
		  }
		  else
			  rtspClient->funcStreamCallback(fReceiveBuffer, frameSize + 4, i_pts, marker,
				  fSubsession.codecName(), rtspClient->pCallbackContext); 

		  //rtspClient->funcStreamCallback(fReceiveBuffer, frameSize + 4, i_pts, marker,
		  //	  fSubsession.codecName(), rtspClient->pCallbackContext);
	  //}

  }
  else if (!strcmp(fSubsession.codecName(), "JPEG") || !strcmp(fSubsession.codecName(), "PCMA"))
  {
	  rtspClient->funcStreamCallback(fReceiveBuffer, frameSize, i_pts, marker,
		  fSubsession.codecName(), rtspClient->pCallbackContext);
  }
  else
  {
	  memmove(fReceiveBuffer + 7, fReceiveBuffer, frameSize);
	  addADTStoPacket(fReceiveBuffer, frameSize + 7);
	  rtspClient->funcStreamCallback(fReceiveBuffer, frameSize+7, i_pts, marker,
		  fSubsession.codecName(), rtspClient->pCallbackContext);
  }
  

#ifdef DEBUG_PRINT_EACH_RECEIVED_FRAME
  if (fStreamId != NULL) envir() << "Stream \"" << fStreamId << "\"; ";
  envir() << fSubsession.mediumName() << "/" << fSubsession.codecName() << ":\tReceived " << frameSize << " bytes";
  if (numTruncatedBytes > 0) envir() << " (with " << numTruncatedBytes << " bytes truncated)";
  char uSecsStr[6+1]; // used to output the 'microseconds' part of the presentation time
  sprintf(uSecsStr, "%06u", (unsigned)presentationTime.tv_usec);
  envir() << ".\tPresentation time: " << (unsigned)presentationTime.tv_sec << "." << uSecsStr;
  if (fSubsession.rtpSource() != NULL && !fSubsession.rtpSource()->hasBeenSynchronizedUsingRTCP()) {
    envir() << "!"; // mark the debugging output to indicate that this presentation time is not RTCP-synchronized
  }
  envir() << "\n";
#endif

  if( numTruncatedBytes > 0 )
  {
        if( fBufferSize < 2000000 )
        {
			fBufferSize*=2;
            //E_WARN_( "lost bytes "<<numTruncatedBytes );
            //E_WARN_( "increasing buffer size to "<<fBufferSize );

			delete[] fReceiveBuffer;
			fReceiveBuffer = new u_int8_t[fBufferSize];

			delete[] fNalBuffer;
			fNalBuffer = new u_int8_t[fBufferSize];
		}
  }
  
  // Then continue, to request the next frame of data:
  continuePlaying();
}

Boolean DummySink::continuePlaying() {
  if (fSource == NULL) return False; // sanity check (should not happen)

  // Request the next frame of data from our input source.  "afterGettingFrame()" will get called later, when it arrives:
  fSource->getNextFrame(fReceiveBuffer, fBufferSize,
                        afterGettingFrame, this,
                        onSourceClosure, this);
  return True;
}



////////// RtspRelayServer implementation /////////

RtspRelayServer* RtspRelayServer
::createNew(UsageEnvironment& env, onRTSPRelayCreationFunc* creationFunc, void *pUser,Port ourPort,
	    int verbosityLevel, char const* applicationName) {
  int ourSocket = setUpOurSocket(env, ourPort);
  if (ourSocket == -1) return NULL;

  return new RtspRelayServer(env, creationFunc, pUser,ourSocket, ourPort, verbosityLevel, applicationName);
}

RtspRelayServer
::RtspRelayServer(UsageEnvironment& env, onRTSPRelayCreationFunc* creationFunc, void *pUser,int ourSocket, Port ourPort,
                                  int verbosityLevel, char const* applicationName)
  : RTSPServer(env, ourSocket, ourPort, NULL, 30/*small reclamationTestSeconds*/),
    fCreationFunc(creationFunc),fUserContext(pUser), fVerbosityLevel(verbosityLevel), fApplicationName(strDup(applicationName)) {
}

RtspRelayServer::~RtspRelayServer() {
  delete[] fApplicationName;
}


char const* RtspRelayServer::allowedCommandNames() {
  return "OPTIONS, REGISTER";
}

Boolean RtspRelayServer::weImplementREGISTER() {
  return True;
}

void RtspRelayServer::implementCmd_REGISTER(char const* url, char const* urlSuffix, int socketToRemoteServer) {
  // Create a new "RTSPClient" object, and call our 'creation function' with it:
//  RTSPClient* newRTSPClient = createNewRTSPClient(url, fVerbosityLevel, fApplicationName, socketToRemoteServer);

  if (fCreationFunc != NULL) (*fCreationFunc)(url,urlSuffix,socketToRemoteServer,fUserContext);
}

