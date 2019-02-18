#include "pch.h"

static ErrorMsg::MuiSource ServiceErrorSource(L"Service", NULL);

// -------------------- Service---------------------------------

Service *Service::instance_;

Service::Service(const wstring &name,
	bool canStop,
	bool canShutdown,
	bool canPauseContinue
) :
	name_(name), statusHandle_(NULL)
{

	// The service runs in its own process.
	status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;

	// The service is starting.
	status_.dwCurrentState = SERVICE_START_PENDING;

	// The accepted commands of the service.
	status_.dwControlsAccepted = 0;
	if (canStop)
		status_.dwControlsAccepted |= SERVICE_ACCEPT_STOP;
	if (canShutdown)
		status_.dwControlsAccepted |= SERVICE_ACCEPT_SHUTDOWN;
	if (canPauseContinue)
		status_.dwControlsAccepted |= SERVICE_ACCEPT_PAUSE_CONTINUE;

	status_.dwWin32ExitCode = NO_ERROR;
	status_.dwServiceSpecificExitCode = 0;
	status_.dwCheckPoint = 0;
	status_.dwWaitHint = 0;
}

Service::~Service()
{ 
}

void Service::run(Erref &err)
{
	err_.reset();
	instance_ = this;

	SERVICE_TABLE_ENTRY serviceTable[] =
	{
		{ (LPWSTR)name_.c_str(), serviceMain },
		{ NULL, NULL }
	};

	if (!StartServiceCtrlDispatcher(serviceTable)) {
		err_ = ServiceErrorSource.mkMuiSystem(GetLastError(), EPEM_SERVICE_DISPATCHER_FAIL, name_.c_str());
	}

	err = err_.copy();
}

void WINAPI Service::serviceMain(
	__in DWORD argc,
	__in_ecount(argc) LPWSTR *argv)
{
	//assert(instance_ != NULL);

	// Register the handler function for the service
	instance_->statusHandle_ = RegisterServiceCtrlHandler(
		instance_->name_.c_str(), serviceCtrlHandler);
	if (instance_->statusHandle_ == NULL)
	{
		instance_->err_.append(ServiceErrorSource.mkMuiSystem(GetLastError(),
			EPEM_SERVICE_HANDLER_REGISTER_FAIL, instance_->name_.c_str()));
		instance_->setStateStoppedSpecific(EPEM_SERVICE_HANDLER_REGISTER_FAIL);
		return;
	}

	// Start the service.
	instance_->setState(SERVICE_START_PENDING);
	instance_->onStart(argc, argv);
}

void WINAPI Service::serviceCtrlHandler(DWORD ctrl)
{
	switch (ctrl)
	{
	case SERVICE_CONTROL_STOP:
		if (instance_->status_.dwControlsAccepted & SERVICE_ACCEPT_STOP) {
			instance_->setState(SERVICE_STOP_PENDING);
			instance_->onStop();
		}
		break;
	case SERVICE_CONTROL_PAUSE:
		if (instance_->status_.dwControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE) {
			instance_->setState(SERVICE_PAUSE_PENDING);
			instance_->onPause();
		}
		break;
	case SERVICE_CONTROL_CONTINUE:
		if (instance_->status_.dwControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE) {
			instance_->setState(SERVICE_CONTINUE_PENDING);
			instance_->onContinue();
		}
		break;
	case SERVICE_CONTROL_SHUTDOWN:
		if (instance_->status_.dwControlsAccepted & SERVICE_ACCEPT_SHUTDOWN) {
			instance_->setState(SERVICE_STOP_PENDING);
			instance_->onShutdown();
		}
		break;
	case SERVICE_CONTROL_INTERROGATE:
		SetServiceStatus(instance_->statusHandle_, &instance_->status_);
		break;
	default:
		break;
	}
}

void Service::setState(DWORD state)
{
	ScopeCritical sc(statusCr_);

	setStateL(state);
}

void Service::setStateL(DWORD state)
{
	status_.dwCurrentState = state;
	status_.dwCheckPoint = 0;
	status_.dwWaitHint = 0;
	SetServiceStatus(statusHandle_, &status_);
}

void Service::setStateStopped(DWORD exitCode)
{
	ScopeCritical sc(statusCr_);

	status_.dwWin32ExitCode = exitCode;
	setStateL(SERVICE_STOPPED);
}

void Service::setStateStoppedSpecific(DWORD exitCode)
{
	ScopeCritical sc(statusCr_);

	status_.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
	status_.dwServiceSpecificExitCode = exitCode;
	setStateL(SERVICE_STOPPED);
}

void Service::bump()
{
	ScopeCritical sc(statusCr_);

	++status_.dwCheckPoint;
	::SetServiceStatus(statusHandle_, &status_);
}

void Service::hintTime(DWORD msec)
{
	ScopeCritical sc(statusCr_);

	++status_.dwCheckPoint;
	status_.dwWaitHint = msec;
	::SetServiceStatus(statusHandle_, &status_);
	status_.dwWaitHint = 0; // won't apply after the next update
}

void Service::onStart(
	__in DWORD argc,
	__in_ecount(argc) LPWSTR *argv)
{
	setState(SERVICE_RUNNING);
}
void Service::onStop()
{
	setStateStopped(NO_ERROR);
}
void Service::onPause()
{
	setState(SERVICE_PAUSED);
}
void Service::onContinue()
{
	setState(SERVICE_RUNNING);
}
void Service::onShutdown()
{
	onStop();
}

