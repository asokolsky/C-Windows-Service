#include "pch.h"

/*++
Copyright (c) 2016 Microsoft Corporation

https://blogs.msdn.microsoft.com/sergey_babkins_blog/2016/12/30/how-to-run-arbitrary-commands-as-a-service/

--*/

// put the includes here

ErrorMsg::Source WaSvcErrorSource(L"WrapSvc", NULL);

class WrapService : public Service
{
protected:
	// The thread that will be waiting for the background process to complete.
	// This handle is owned by this class.
	HANDLE waitThread_;

public:
	shared_ptr<Logger> logger_; // the logger
	shared_ptr<LogEntity> entity_; // mostly a placeholder for now

	// NONE OF THE HANDLES BELOW ARE OWNED HERE.
	// Whoever created them should close them after disposing of this object.

	// Information about the running background process.
	// The code that starts it is responsible for filling this
	// field directly.
	PROCESS_INFORMATION pi_;
	// The event used to signal the stop request to the process,
	HANDLE stopEvent_;

	// Don't forget to fill in pi_ with the information from the started
	// background process after constructing this object!
	//
	// name - service name
	// logger - objviously, used for logging the messages
	// stopEvent - event object used to signal the stop request
	WrapService(
		__in const std::wstring &name,
		__in shared_ptr<Logger> logger,
		__in HANDLE stopEvent
	)
		: Service(name, true, true, false),
		waitThread_(INVALID_HANDLE_VALUE),
		logger_(logger),
		stopEvent_(stopEvent)
	{
		ZeroMemory(&pi_, sizeof(pi_));
	}

	~WrapService()
	{
		if (waitThread_ != INVALID_HANDLE_VALUE) {
			CloseHandle(waitThread_);
		}
	}

	void log(
		__in Erref err,
		__in Logger::Severity sev)
	{
		logger_->log(err, sev, entity_);
	}

	virtual void onStart(
		__in DWORD argc,
		__in_ecount(argc) LPWSTR *argv)
	{
		setStateRunning();

		// start the thread that will wait for the background process
		waitThread_ = CreateThread(NULL,
			0, // do we need to change the stack size?
			&waitForProcess,
			(LPVOID)this,
			0, NULL);

		if (waitThread_ == INVALID_HANDLE_VALUE) {
			log(WaSvcErrorSource.mkSystem(GetLastError(), 1, L"Failed to create the thread that will wait for the background process:"),
				Logger::SV_ERROR);

			if (!SetEvent(stopEvent_)) {
				log(WaSvcErrorSource.mkSystem(GetLastError(), 1, L"Failed to set the event to stop the service:"),
					Logger::SV_ERROR);
			}
			WaitForSingleObject(pi_.hProcess, INFINITE); // ignore any errors...

			setStateStopped(1);
			return;
		}
	}

	// The background thread that waits for the child process to complete.
	// arg - the WrapService object where the status gets reported
	DWORD static waitForProcess(LPVOID arg)
	{
		WrapService *svc = (WrapService *)arg;

		DWORD status = WaitForSingleObject(svc->pi_.hProcess, INFINITE);
		if (status == WAIT_FAILED) {
			svc->log(
				WaSvcErrorSource.mkSystem(GetLastError(), 1, L"Failed to wait for the process completion:"),
				Logger::SV_ERROR);
		}

		DWORD exitCode = 1;
		if (!GetExitCodeProcess(svc->pi_.hProcess, &exitCode)) {
			svc->log(
				WaSvcErrorSource.mkSystem(GetLastError(), 1, L"Failed to get the process exit code:"),
				Logger::SV_ERROR);
		}

		svc->log(
			WaSvcErrorSource.mkString(0, L"The process exit code is: %d.", exitCode),
			Logger::SV_INFO);

		svc->setStateStopped(exitCode);
		return exitCode;
	}

	virtual void onStop()
	{
		if (!SetEvent(stopEvent_)) {
			log(WaSvcErrorSource.mkSystem(GetLastError(), 1, L"Failed to set the event to stop the service:"),
				Logger::SV_ERROR);
			// not much else to be done?
			return;
		}

		DWORD status = WaitForSingleObject(waitThread_, INFINITE);
		if (status == WAIT_FAILED) {
			log(WaSvcErrorSource.mkSystem(GetLastError(), 1, L"Failed to wait for thread that waits for the process completion:"),
				Logger::SV_ERROR);
			// not much else to be done?
			return;
		}

		// the thread had already set the exit code, so nothing more to do
	}
};

int
__cdecl
wmain(
	__in long argc,
	__in_ecount(argc) PWSTR argv[]
)
/*++

Routine Description:

	This is the Win32 entry point for the application.

Arguments:

	argc - The number of command line arguments.

	argv - The command line arguments.

Return Value:

	Zero on success, non-zero otherwise.

--*/
{
	Erref err;
	wstring result;
	HRESULT hr;
	DWORD status;

#define DEFAULT_GLOBAL_EVENT_PREFIX L"Global\\Service"

	shared_ptr<Logger> logger = make_shared<StdoutLogger>(Logger::SV_DEBUG); // will write to stdout
	std::shared_ptr<LogEntity> logEntity = NULL;

	Switches switches(WaSvcErrorSource.mkString(0,
		L"Wrapper to run any program as a service.\n"
		L"  WrapSvc [switches] -- wrapped command\n"
		L"The rest of the arguments constitute the command that will start the actual service process.\n"
		L"The arguments will be passed directly to CreateProcess(), so the name of the executable\n"
		L"must constitute the full path and full name with the extension.\n"
		L"The switches are:\n"));
	auto swName = switches.addMandatoryArg(
		L"name", WaSvcErrorSource.mkString(0, L"Name of the service being started."));
	auto swEvent = switches.addArg(
		L"event", WaSvcErrorSource.mkString(0, L"Name of the event that will be used to request the service stop. If not specified, will default to " DEFAULT_GLOBAL_EVENT_PREFIX "<ServiceName>, where <ServiceName> is taken from the switch -name."));
	auto swOwnLog = switches.addArg(
		L"ownLog", WaSvcErrorSource.mkString(0, L"Name of the log file where the log of the wrapper's own will be switched."));
	auto swSvcLog = switches.addArg(
		L"svcLog", WaSvcErrorSource.mkString(0, L"Name of the log file where the stdout and stderr of the service process will be switched."));
	auto swAppend = switches.addBool(
		L"append", WaSvcErrorSource.mkString(0, L"Use the append mode for the logs, instead of overwriting."));

	switches.parse(argc, argv);
	// try to honor the log switch if it's parseable even if the rest aren't
	if (swOwnLog->on_) {
		// reopen the logger
		if (!swAppend->on_)
			DeleteFileW(swOwnLog->value_); // ignore the errors
		auto newlogger = make_shared<FileLogger>(swOwnLog->value_, Logger::SV_DEBUG);
		logger->logAndExitOnError(newlogger->error(), NULL); // fall through if no error
		logger = newlogger;
	}

	logger->logAndExitOnError(switches.err_, NULL);

	// Since the underlying arguments aren't actually parsed on Windows but are
	// passed as a single string, find this string directly from Windows, for passing
	// through.
	LPWSTR cmdline = GetCommandLineW();
	PWSTR passline = wcsstr(cmdline, L" --");
	if (passline == NULL) {
		err = WaSvcErrorSource.mkString(1, L"Cannot find a '--' in the command line '%ls'.", cmdline);
		logger->logAndExitOnError(err, NULL);
	}
	passline += 3;
	while (*passline != 0 && iswspace(*passline))
		++passline;

	if (*passline == 0) {
		err = WaSvcErrorSource.mkString(1, L"The part after a '--' is empty in the command line '%ls'.", cmdline);
		logger->logAndExitOnError(err, NULL);
	}

	logger->log(
		WaSvcErrorSource.mkString(0, L"--- WaSvc started."),
		Logger::SV_INFO, NULL);

	// Create the service stop request event

	wstring evname;
	if (swEvent->on_) {
		evname = swEvent->value_;
	}
	else {
		evname = DEFAULT_GLOBAL_EVENT_PREFIX;
		evname.append(swName->value_);
	}
	logger->log(
		WaSvcErrorSource.mkString(0, L"The stop event name is '%ls'", evname.c_str()),
		Logger::SV_INFO, NULL);

	HANDLE stopEvent = CreateEventW(NULL, TRUE, FALSE, evname.c_str());
	if (stopEvent == INVALID_HANDLE_VALUE) {
		err = WaSvcErrorSource.mkSystem(GetLastError(), 1, L"Failed to create the event '%ls':", evname.c_str());
		logger->logAndExitOnError(err, NULL);
	}
	if (!ResetEvent(stopEvent)) {
		err = WaSvcErrorSource.mkSystem(GetLastError(), 1, L"Failed to reset the event '%ls' before starting the service:", evname.c_str());
		logger->logAndExitOnError(err, NULL);
	}

	auto svc = make_shared<WrapService>(swName->value_, logger, stopEvent);

	logger->log(
		WaSvcErrorSource.mkString(0, L"The internal process command line is '%ls'", passline),
		Logger::SV_INFO, NULL);

	STARTUPINFO si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	HANDLE newlog = INVALID_HANDLE_VALUE;
	if (swSvcLog->on_) {
		if (swOwnLog->on_ && !_wcsicmp(swOwnLog->value_, swSvcLog->value_)) {
			err = WaSvcErrorSource.mkString(1, L"The wrapper's own log and the service's log must not be both redirected to the same file '%ls'.", swSvcLog->value_);
			logger->logAndExitOnError(err, NULL);
		}

		if (!swAppend->on_)
			DeleteFileW(swSvcLog->value_); // ignore the errors

		SECURITY_ATTRIBUTES inheritable = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

		newlog = CreateFileW(swSvcLog->value_, swAppend->on_ ? (FILE_GENERIC_WRITE | FILE_APPEND_DATA) : GENERIC_WRITE,
			FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
			&inheritable, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (newlog == INVALID_HANDLE_VALUE) {
			err = WaSvcErrorSource.mkSystem(GetLastError(), 1, L"Open of the service log file '%ls' failed:", swSvcLog->value_);
			logger->logAndExitOnError(err, NULL);
		}
		if (swAppend->on_)
			SetFilePointer(newlog, 0, NULL, FILE_END);

		si.dwFlags |= STARTF_USESTDHANDLES;
		si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		si.hStdOutput = newlog;
		si.hStdError = newlog;
	}

	if (!CreateProcess(NULL, passline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &svc->pi_)) {
		err = WaSvcErrorSource.mkSystem(GetLastError(), 1, L"Failed to create the child process.");
		logger->logAndExitOnError(err, NULL);
	}

	if (newlog != INVALID_HANDLE_VALUE) {
		if (!CloseHandle(newlog)) {
			logger->log(
				WaSvcErrorSource.mkSystem(GetLastError(), 2, L"Failed to close the old handle for stderr."),
				Logger::SV_ERROR, NULL); // don't exit
		}
	}

	logger->log(
		WaSvcErrorSource.mkString(0, L"Started the process."),
		Logger::SV_INFO, NULL);

	svc->run(err);
	if (err) {
		logger->log(err, Logger::SV_ERROR, NULL);
		if (!SetEvent(stopEvent)) {
			logger->log(WaSvcErrorSource.mkSystem(GetLastError(), 1, L"Failed to set the event to stop the service:"),
				Logger::SV_ERROR, NULL);
		}
		WaitForSingleObject(svc->pi_.hProcess, INFINITE); // ignore any errors...
		exit(1);
	}

	CloseHandle(svc->pi_.hProcess);
	CloseHandle(svc->pi_.hThread);
	CloseHandle(stopEvent);

	logger->log(
		WaSvcErrorSource.mkString(0, L"--- WaSvc stopped."),
		Logger::SV_INFO, NULL);
	return 0;
}
