#ifndef RTSPPROTOCOL_H
#define RTSPPROTOCOL_H

#include <UsageEnvironment.hh>
#include <BasicUsageEnvironment.hh>
//#include <GroupsockHelper.hh>
#include <liveMedia.hh>

typedef void (*StreamCallback) (unsigned char *pBuffer, unsigned int dwBufSize,unsigned int timestamp,
									  unsigned int marker,const char* payloadtype,void* pContext);
								

//void* NewRtspClient(const char* rtspUrl,StreamCallback funcStreamCallback,void *pCallbackContext);
//int RunRtspClient(void *rtspClient);
RTSPClient* openURL(UsageEnvironment& env, char const* rtspURL,int socketNumToServer,StreamCallback funcStreamCallback,void *pCallbackContext);
int CloseRtspClient(RTSPClient *rtspClient);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef void onRTSPRelayCreationFunc(char const* url, char const* urlSuffix, int socketToRemoteServer,void *pUser);

class RtspRelayServer: public RTSPServer {
public:
  static RtspRelayServer* createNew(UsageEnvironment& env, onRTSPRelayCreationFunc* creationFunc,void *pUser,
						    Port ourPort = 0, int verbosityLevel = 0, char const* applicationName = NULL);
      // If ourPort.num() == 0, we'll choose the port number ourself.  (Use the following function to get it.)
  portNumBits serverPortNum() const { return ntohs(fRTSPServerPort.num()); }

protected:
  RtspRelayServer(UsageEnvironment& env, onRTSPRelayCreationFunc* creationFunc,void *pUser, int ourSocket, Port ourPort,
				  int verbosityLevel, char const* applicationName);
      // called only by createNew();
  virtual ~RtspRelayServer();


protected: // redefined virtual functions
  virtual char const* allowedCommandNames(); // we support "OPTIONS" and "REGISTER" only
  virtual Boolean weImplementREGISTER(); // redefined to return True
  virtual void implementCmd_REGISTER(char const* url, char const* urlSuffix, int socketToRemoteServer);

private:
  onRTSPRelayCreationFunc* fCreationFunc;
  void* fUserContext;
  int fVerbosityLevel;
  char* fApplicationName;
};

#endif