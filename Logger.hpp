/*++

Copyright (c) 2016 Microsoft Corporation

https://blogs.msdn.microsoft.com/sergey_babkins_blog/2016/12/30/error-handling-part-6-etw-logging-example/

--*/


class LogEntity
{
	// Normally stored in a shared_ptr.
	// The address of this object is used as a token used to group
	// the messages from this entity inside the loggers.
public:
	LogEntity(const std::wstring &name) :
		name_(name)
	{
	}

	std::wstring name_; // name of the entity
};

class Logger
{
public:
	enum Severity {
		SV_DEBUG,
		SV_VERBOSE, // the detailed information
		SV_INFO,
		SV_WARNING,
		SV_ERROR,
		SV_NEVER, // pseudo-severity, used to indicate that the logger logs nothing
		SV_DEFAULT_MIN = SV_INFO // the default lowest severity to be logged
	};

	// Specify the minimum severity to not throw away.
	Logger(Severity minSeverity) :
		minSeverity_(minSeverity)
	{
	}

	// A Logger would normally be reference-counted by shared_ptr.
	virtual ~Logger();

	// Log a message. Works even on a NULL pointer to a logger
	// (i.e. if the logger is not available, throws away the messages).
	//
	// err - the error object
	// sev - the severity (the logger might decide to throw away the
	//      messages below some level)
	// entity - description of the entity that reported the error;
	//      may be NULL
	void log(
		__in Erref err,
		__in Severity sev,
		__in_opt std::shared_ptr<LogEntity> entity
	)
	{
		if (this != NULL && err) // ignore the no-errors
			logBody(err, sev, entity);
	}

	// May be called periodically in case if the logger
	// needs to do some periodic processing, such as flushing
	// the buffers. The EtwLogger uses this method to send
	// the collected backlog after the provider gets enabled
	// even if there are no more log messages written.
	// The default implementation does nothing.
	virtual void poll();

	// A special-case hack for the small tools:
	// If this error reference is not empty, log it and exit(1).
	// As another special case, if this logger object is NULL,
	// the error gets printed directly on stdout.
	// The severity here is always SV_ERROR.
	void logAndExitOnError(
		__in Erref err,
		__in_opt std::shared_ptr<LogEntity> entity
	);

	// The internal implementation of log()
	// This function must be internally synchronized, since it will
	// be called from multiple threads. Implemented in subclasses.
	virtual void logBody(
		__in Erref err,
		__in Severity sev,
		__in_opt std::shared_ptr<LogEntity> entity
	) = 0;

	// The smarter check that works even on a NULL pointer.
	Severity getMinSeverity() const
	{
		if (this == NULL)
			return SV_NEVER;
		else
			return minSeverity_;
	}

	// Check that the logger will accept messages of a given severity.
	// Can be used to avoid printing message sthat will be thrown away.
	bool allowsSeverity(Severity sv) const
	{
		if (this == NULL)
			return false;
		else
			return (sv >= minSeverity_);
	}

	virtual void setMinSeverity(Severity sv);

	// Translate the severity into an one-letter indication.
	// Returns '?' for an invalid value.
	static WCHAR oneLetterSeverity(Severity sv);

	// Translate the severity into a full name.
	// Returns NULL for an invalid value.
	static const WCHAR *strSeverity(Severity sv);

	// Translate the human-readable name of severity to
	// enum value.
	// Returns SV_NEVER if it cannot find a name.
	static Severity severityFromName(const std::wstring &svname);

	// Returns the list of all supported severity levels.
	static std::wstring listAllSeverities();

public:
	volatile Severity minSeverity_; // the lowest severity that passes through the logger;
		// normally set once and then read-only, the callers may use it to
		// optimize and skip the messages that will be thrown away
};

// A shortcut if the message is intended only for logging:
// skips the message creation if the logger won't record it anyway.
#define LOG_SHORTCUT(logger, severity, entity, err) do { \
    if (logger->allowsSeverity(severity)) { \
        logger->log(err, severity, entity); \
    } } while(0)


class EtwLogger : public Logger
{
public:
	// The approximate limit on the message string length
	// in one ETW message, in characters. The message chains longer
	// than that will be split up. The individual messages in the
	// chain will not be split, so if a real long message happens,
	// it will hit the ETW limit and be thrown away.
	//
	// Since the strings are in Unicode, that the character number
	// must be multiplied by 2 to get the byte size.
	// Keep in mind that the ETW message size limit is 64KB, including
	// all the headers.
	enum { STRING_LIMIT = 5 * 1000 };
	// The maximum expected number of fields in the message,
	// which drives the limit on the number of the error codes per message
	// (i.e. the nesting depth of the Erref).
	enum { MSG_FIELD_LIMIT = 100 };

	// guid - GUID of the ETW provider (must match the .man file)
	// minSeverity - the minimum severity to not throw away.
	//
	// The errors are kept, and can be extracted with error().
	// A EtwLogger with errors cannot be used.
	EtwLogger(
		LPCGUID guid,
		_In_ Severity minSeverity = SV_DEFAULT_MIN
	);

	// Closes the file.
	~EtwLogger();

	// Close the logger at any time (no logging is possible after that).
	// The errors get recorded and can be extracted with error().
	void close();

	// from Logger
	void logBody(
		__in Erref err,
		__in Severity sev,
		__in_opt std::shared_ptr<LogEntity> entity
	);
	void poll();

	// Get the logger's fatal error. Obviously, it would have to be reported
	// in some other way.
	Erref error()
	{
		return err_;
	}

protected:
	// Callback from ETW to enable and disable logging.
	static void NTAPI callback(
		_In_ LPCGUID sourceId,
		_In_ ULONG isEnabled,
		_In_ UCHAR level,
		_In_ ULONGLONG matchAnyKeyword,
		_In_ ULONGLONG matchAllKeywords,
		_In_opt_ PEVENT_FILTER_DESCRIPTOR filterData,
		_In_opt_ PVOID context
	);

	// The actual logging. The caller must check that
	// the logging is already enabled and that the severity
	// level is allowed.
	// The caller must also hold cr_.
	void logBodyInternalL(
		__in Erref err,
		__in Severity sev,
		__in_opt std::shared_ptr<LogEntity> entity
	);

	// Check if anything is in the backlog, and forward it.
	// The caller must also hold cr_.
	void processBacklogL();

	enum {
		// Up to how many entries to keep on the backlog.
		BACKLOG_LIMIT = 4096,
	};
	struct BacklogEntry {
	public:
		Erref err_;
		Severity sev_;
		std::shared_ptr<LogEntity> entity_;

	public:
		BacklogEntry(
			__in Erref err,
			__in Severity sev,
			__in_opt std::shared_ptr<LogEntity> entity
		) :
			err_(err),
			sev_(sev),
			entity_(entity)
		{
        }
	};

protected:
	Critical cr_;			// synchronizes the object
	std::wstring guidName_; // for error reports, GUID in string format
	REGHANDLE h_;			// handle for logging
	Erref err_;				// the recorded fatal error
	Severity origMinSeverity_; // the minimal severity as was set on creation
	std::deque<BacklogEntry> backlog_; // backlog of messages to send when the provider becomes enabled
	bool enabled_; // whether anyone is listening in ETW
};

class StdoutLogger : public Logger
{
public:
    StdoutLogger(
        _In_ Severity minSeverity = SV_DEFAULT_MIN
    );
    ~StdoutLogger();

    // The internal implementation of log()
    // This function must be internally synchronized, since it will
    // be called from multiple threads. Implemented in subclasses.
    void logBody(
        __in Erref err,
        __in Severity sev,
        __in_opt std::shared_ptr<LogEntity> entity
    );

};

#define NTSTATUS ULONG

#define EVENT_CONTROL_CODE_DISABLE_PROVIDER 0
#define EVENT_CONTROL_CODE_ENABLE_PROVIDER 1
#define EVENT_CONTROL_CODE_CAPTURE_STATE 2

#define TRACE_LEVEL_CRITICAL 1
#define TRACE_LEVEL_ERROR 2
#define TRACE_LEVEL_WARNING 3
#define TRACE_LEVEL_INFORMATION 4
#define TRACE_LEVEL_VERBOSE 5
