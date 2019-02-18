#include "pch.h"

static ErrorMsg::MuiSource LogErrorSource(L"Service", NULL);

// -------------------- Logger ---------------------------------

Logger::~Logger()
{
}

void Logger::poll()
{
}

void Logger::logAndExitOnError(
	__in Erref err,
	__in_opt std::shared_ptr<LogEntity> entity
)
{
	if (!err)
		return;
	if (this != NULL) 
	{
		log(err, Severity::SV_ERROR, entity);
	}
	else 
	{
		wprintf(L"%ls", err->toString().c_str());
	}
	exit(1);
}

void Logger::setMinSeverity(Severity sv)
{
	minSeverity_ = sv;
}

WCHAR Logger::oneLetterSeverity(Severity sv)
{
	switch (sv) 
	{
		case SV_DEBUG:
			return L'D';
		case SV_VERBOSE:
			return L'V';
		case SV_INFO:
			return L'I';
		case SV_WARNING:
			return L'W';
		case SV_ERROR:
			return L'E';
	}
	return L'?';
}

const WCHAR *Logger::strSeverity(Severity sv)
{
	switch (sv) {
		case SV_DEBUG:
			return L"DEBUG";
		case SV_VERBOSE:
			return L"VERBOSE";
		case SV_INFO:
			return L"INFO";
		case SV_WARNING:
			return L"WARNING";
		case SV_ERROR:
			return L"ERROR";
	}
	return NULL;
}

Logger::Severity Logger::severityFromName(const std::wstring &svname)
{
	if (svname.empty())
		return SV_NEVER;
	// just look at the first letter
	switch (towlower(svname[0])) 
	{
		case 'd':
			return SV_DEBUG;
		case 'v':
			return SV_VERBOSE;
		case 'i':
			return SV_INFO;
		case 'w':
			return SV_WARNING;
		case 'e':
			return SV_ERROR;
	};
	return SV_NEVER;
}

std::wstring Logger::listAllSeverities()
{
	std::wstring result;

	for (int sv = 0; sv < (int)SV_NEVER; ++sv) 
	{
		strListSep(result);
		result.append(strSeverity((Severity)sv));
	}

	return result;
}

/**
 *  EtwLogger
 */

EtwLogger::EtwLogger(
	LPCGUID guid,
	_In_ Severity minSeverity
) :
	Logger(minSeverity),
	guidName_(strFromGuid(*guid)), h_(NULL),
	origMinSeverity_(minSeverity), enabled_(false)
{
	NTSTATUS status = EventRegister(guid, &callback, this, &h_);
	if (status != STATUS_SUCCESS) {
		err_ = LogErrorSource.mkMuiSystem(status, EPEM_LOG_EVENT_REGISTER_FAIL,
			guidName_.c_str());
		return;
	}
}

EtwLogger::~EtwLogger()
{
	close();
}

void EtwLogger::close()
{
	ScopeCritical sc(cr_);

	if (h_ == NULL)
		return;

	NTSTATUS status = EventUnregister(h_);
	if (status != STATUS_SUCCESS) 
	{
		Erref newerr = LogErrorSource.mkMuiSystem(GetLastError(), EPEM_LOG_EVENT_UNREGISTER_FAIL, guidName_.c_str());
		err_.append(newerr);
	}
	h_ = NULL;
	enabled_ = false;
}

void EtwLogger::logBody(
	__in Erref err,
	__in Severity sev,
	__in_opt std::shared_ptr<LogEntity> entity
)
{
	ScopeCritical sc(cr_);

	if (sev < minSeverity_ || h_ == NULL)
		return;

	if (enabled_)
	{
		// The backlog cannot be written from the callback when
		// the logger gets enabled, so write on the next message.
		processBacklogL();
		logBodyInternalL(err, sev, entity);
	}
	else 
	{
		backlog_.push_back(BacklogEntry(err, sev, entity));
		while (backlog_.size() > BACKLOG_LIMIT)
			backlog_.pop_front();
	}
}

void EtwLogger::poll()
{
	ScopeCritical sc(cr_);

	if (h_ == NULL || !enabled_)
		return;

	processBacklogL();
}

void EtwLogger::processBacklogL()
{
	while (!backlog_.empty()) 
	{
		if (h_ != NULL) 
		{
			BacklogEntry &entry = backlog_.front();
			logBodyInternalL(entry.err_, entry.sev_, entry.entity_);
		}
		backlog_.pop_front();
	}
}

void EtwLogger::logBodyInternalL(
	__in Erref err,
	__in Severity sev,
	__in_opt std::shared_ptr<LogEntity> entity
)
{
	const wchar_t *entname = L"[general]";
	if (entity && !entity->name_.empty())
		entname = entity->name_.c_str();

	PCEVENT_DESCRIPTOR event;

	switch (sev) 
	{
	case SV_ERROR:
		event = &ETWMSG_LOG_INST_ERROR2;
		break;
	case SV_WARNING:
		event = &ETWMSG_LOG_INST_WARNING2;
		break;
	default:
		// The .man validation doesn't allow to use any other levels for the
		// Admin messages, so just sweep everything else into INFO.
		// Theoretically, VERBOSE and DEBUG can be placed into a separate
		// channel but doing it well will require more thinking.
		event = &ETWMSG_LOG_INST_INFO2;
		break;
	}

	wstring text;
	Erref cur, next;
	for (cur = err; cur; cur = next) 
	{
		text.clear();
		text.push_back(oneLetterSeverity(sev));
		text.push_back(L' ');
		if (next) 
		{
			text.append(L"(continued)\n  ");
		}
		text.append(cur->toLimitedString(STRING_LIMIT, next));

		uint32_t intval[MSG_FIELD_LIMIT];
		EVENT_DATA_DESCRIPTOR ddesc[MSG_FIELD_LIMIT];
		int fcount = 0;

		EventDataDescCreate(ddesc + fcount, text.c_str(), (ULONG)(sizeof(WCHAR) * (text.size() + 1)));
		++fcount;

		intval[fcount] = (err.getCode() & 0x3FFFFFFF);
		EventDataDescCreate(ddesc + fcount, intval + fcount, (ULONG)(sizeof(uint32_t)));
		++fcount;

		// build the message array
		uint32_t *szptr = intval + fcount;
		*szptr = 0; // the value will be updated as the array gets built
		EventDataDescCreate(ddesc + fcount, intval + fcount, (ULONG)(sizeof(uint32_t)));
		++fcount;

		// can the whole array be placed in a single ddesc instead?
		for (Erref eit = cur; eit != next && fcount < MSG_FIELD_LIMIT; eit = eit->chain_) 
		{
			intval[fcount] = (eit.getCode() & 0x3FFFFFFF);
			EventDataDescCreate(ddesc + fcount, intval + fcount, (ULONG)(sizeof(uint32_t)));
			++fcount;
			++*szptr;
		}

		NTSTATUS status = EventWrite(h_, event, fcount, ddesc);

		switch (status) 
		{
		case STATUS_SUCCESS:
			break;
		case ERROR_ARITHMETIC_OVERFLOW:
		case ERROR_MORE_DATA:
		case ERROR_NOT_ENOUGH_MEMORY:
		case STATUS_LOG_FILE_FULL:
			// TODO: some better reporting of these non-fatal errors
			break;
		default:
			Erref newerr = LogErrorSource.mkMuiSystem(status, EPEM_LOG_EVENT_WRITE_FAIL, guidName_.c_str());
			err_.append(newerr);
			close(); // and give up
			return;
		}
	}
}

void NTAPI EtwLogger::callback(
	_In_ LPCGUID sourceId,
	_In_ ULONG isEnabled,
	_In_ UCHAR level,
	_In_ ULONGLONG matchAnyKeyword,
	_In_ ULONGLONG matchAllKeywords,
	_In_opt_ PEVENT_FILTER_DESCRIPTOR filterData,
	_In_opt_ PVOID context
)
{
	EtwLogger *logger = (EtwLogger *)context;
	if (logger == NULL)
		return;

	ScopeCritical sc(logger->cr_);

	switch (isEnabled)
	{
	case EVENT_CONTROL_CODE_DISABLE_PROVIDER:
		logger->enabled_ = false;
		logger->minSeverity_ = SV_NEVER;
		break;
	case EVENT_CONTROL_CODE_ENABLE_PROVIDER:
		logger->enabled_ = true;
		switch (level) {
		case TRACE_LEVEL_CRITICAL:
		case TRACE_LEVEL_ERROR:
			logger->minSeverity_ = SV_ERROR;
			break;
		case TRACE_LEVEL_WARNING:
			logger->minSeverity_ = SV_WARNING;
			break;
		case TRACE_LEVEL_INFORMATION:
			logger->minSeverity_ = SV_INFO;
			break;
		case TRACE_LEVEL_VERBOSE:
			logger->minSeverity_ = SV_VERBOSE;
			break;
		default: // the level value may be any, up to 255
			logger->minSeverity_ = SV_DEBUG;
			break;
		}
		if ((int)logger->origMinSeverity_ > (int)logger->minSeverity_)
			logger->minSeverity_ = logger->origMinSeverity_;
		break;
	default:
		// do nothing
		break;
	}
}

/**
 *  StdoutLogger
 */

StdoutLogger::~StdoutLogger()
{

}

// The internal implementation of log()
// This function must be internally synchronized, since it will
// be called from multiple threads. Implemented in subclasses.
void StdoutLogger::logBody(
    __in Erref err,
    __in Severity sev,
    __in_opt std::shared_ptr<LogEntity> entity)
{

}

