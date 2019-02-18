// SimpleService.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"


/**
 *  MyService is derived from Service
 *
 */
class MyService : public Service
{
protected:
	// The background thread that will be executing the application.
	// This handle is owned by this class.
	HANDLE appThread_;

public:
	// The exit code that will be set by the application thread on exit.
	DWORD exitCode_;

	// name - service name
	MyService(__in const std::wstring &name) :
		Service(name, true, true, false),
		appThread_(INVALID_HANDLE_VALUE),
		exitCode_(1) // be pessimistic
	{
	}

	~MyService();
	void onStart(__in DWORD argc, __in_ecount(argc) LPWSTR *argv);
	void onStop();
};

MyService::~MyService()
{
	if (appThread_ != INVALID_HANDLE_VALUE) {
		CloseHandle(appThread_);
	}
}

/**
 * Main entry point when run as a service
 */
DWORD WINAPI serviceMainFunction(LPVOID lpParam)
{
	Erref err;
	MyService *svc = (MyService *)lpParam;
	svc->run(err);
	if (err)
	{
		//logger->log(err, Logger::SV_ERROR, NULL);
		return 1;
	}
	return 0;
}

void MyService::onStart(
	__in DWORD argc,
	__in_ecount(argc) LPWSTR *argv)
{
	setStateRunning();

	// start the thread that will execute the application
	appThread_ = CreateThread(NULL,
		0, // do we need to change the stack size?
		&serviceMainFunction,
		(LPVOID)this,
		0, NULL);

	if (appThread_ == INVALID_HANDLE_VALUE) {
		//log(WaSvcErrorSource.mkSystem(GetLastError(), 1, L"Failed to create the application thread:"),
		//	Logger::SV_ERROR);
		setStateStopped(1);
		return;
	}
}

void MyService::onStop()
{
	// ... somehow tell the application thread to stop ...
	DWORD status = WaitForSingleObject(appThread_, INFINITE);
	if (status == WAIT_FAILED) {
		//log(WaSvcErrorSource.mkSystem(GetLastError(), 1, L"Failed to wait for the application thread:"),
		//	Logger::SV_ERROR);
		// presumably exitCode_ already contains some reason at this point
	}
	// exitCode_ should be set by the application thread on exit
	setStateStopped(exitCode_);
}

/**
 * Main Entry Point when run as console app
 */
int __cdecl wmain(
	__in long argc,
	__in_ecount(argc) PWSTR argv[]
)
{
	// ... initialize the logger, parse the arguments etc ...

	auto svc = make_shared<MyService>("MyService");
	Erref err;
	svc->run(err);
	if (err)
	{
		//logger->log(err, Logger::SV_ERROR, NULL);
		exit(1);
	}
	return 0;
}

