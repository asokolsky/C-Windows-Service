#pragma once

//#define DLLEXPORT __declspec( dllexport )
#define DLLEXPORT


class DLLEXPORT Service
{
public:
	// The way the services work, there can be only one Service object
	// in the process. 
	Service(const std::wstring &name,
		bool canStop,
		bool canShutdown,
		bool canPauseContinue);

	virtual ~Service();

	// Run the service. Returns after the service gets stopped.
	// When the Service object gets started,
	// it will remember the instance pointer in the instance_ static
	// member, and use it in the callbacks.
	// The errors are reported back in err.
	void run(Erref &err);

	// Change the service state. Don't use it for SERVICE_STOPPED,
	// do that through the special versions.
	// Can be called only while run() is running.
	void setState(DWORD state);
	// The convenience versions.
	void setStateRunning()
	{
		setState(SERVICE_RUNNING);
	}
	void setStatePaused()
	{
		setState(SERVICE_PAUSED);
	}
	// The stopping is more compilcated: it also sets the exit code.
	// Which can be either general or a service-specific error code.
	// The success indication is the general code NO_ERROR.
	// Can be called only while run() is running.
	void setStateStopped(DWORD exitCode);
	void setStateStoppedSpecific(DWORD exitCode);

	// On the lengthy operations, periodically call this to tell the
	// controller that the service is not dead.
	// Can be called only while run() is running.
	void bump();

	// Can be used to set the expected length of long operations.
	// Also does the bump.
	// Can be called only while run() is running.
	void hintTime(DWORD msec);

	// Methods for the subclasses to override.
	// The base class defaults set the completion state, so the subclasses must
	// either call them at the end of processing (maybe after some wait, maybe
	// from another thread) or do it themselves.
	// The pending states (where applicable) will be set before these methods
	// are called.
	// onStart() is responsible for actually starting the application
	virtual void onStart(
		__in DWORD argc,
		__in_ecount(argc) LPWSTR *argv);
	virtual void onStop(); // sets the success exit code
	virtual void onPause();
	virtual void onContinue();
	virtual void onShutdown(); // calls onStop()

protected:
	// The callback for the service start.
	static void WINAPI serviceMain(
		__in DWORD argc,
		__in_ecount(argc) LPWSTR *argv);
	// The callback for the requests.
	static void WINAPI serviceCtrlHandler(DWORD ctrl);

	// the internal version that expects the caller to already hold statusCr_
	void setStateL(DWORD state);

protected:
	/** this is a singleton! */
	static Service *instance_;

	std::wstring name_; // service name

	Critical statusCr_; // protects the status setting
	SERVICE_STATUS_HANDLE statusHandle_; // handle used to report the status
	SERVICE_STATUS status_; // the current status

	Critical errCr_; // protects the error handling
	Erref err_; // the collected errors

private:
	Service();
	Service(const Service &);
	void operator=(const Service &);
};

