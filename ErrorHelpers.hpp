#pragma once

/*++
//-------------------------------- ErrorHelpers.hpp ---------------------------

Copyright (c) 2014 Microsoft Corporation

--*/

using namespace std;

class LogEntity;
class Logger;

//////////////// general helper functions /////////////////////////////

// Like [v]sprintf() but returns a string with the result.
std::wstring __cdecl wstrprintf(
	_In_z_ _Printf_format_string_ const WCHAR *fmt,
	...
);
std::wstring __cdecl vwstrprintf(
	_In_z_ const WCHAR *fmt,
	__in va_list args);

// Like wstrprintf, only appends to an existing string object
// instead of making a new one.
void __cdecl wstrAppendF(
	__inout std::wstring &dest, // destination string to append to
	_In_z_ _Printf_format_string_ const WCHAR *fmt,
	...
);
void __cdecl vwstrAppendF(
	__inout std::wstring &dest, // destination string to append to
	_In_z_ const WCHAR *fmt,
	__in va_list args);

////////////////////// ErrorMsg ///////////////////////////////////////

class ErrorMsg;

// It's convenient to report the errors as a single counted reference,
// returning NULL if there is no error.
class Erref : public std::shared_ptr<ErrorMsg>
{
public:
	Erref()
	{

	}

	Erref(const std::shared_ptr<ErrorMsg> &other) :
		std::shared_ptr<ErrorMsg>(other)
	{ 

	}

	// copy operator= inherited from std::shared_ptr

	// The inlined methods are defined below, after ErrorMsg definition.

	// Check whether there is an error referenced.
	bool hasError();
	// Get the error code, if any (or return ERROR_SUCCESS on on error).
	DWORD getCode();
	// Get the chained error. Returns NULL if either the chained error
	// is not present, or this reference is NULL.
	Erref getChain();
	// Get the error code of the chained error, if any (or return ERROR_SUCCESS on on error).
	DWORD getChainCode();

	// Splice the chain contained in the other error object between
	// this object and the rest of its chain.
	// If the other object is NULL, does nothing.
	// If this object is NULL, makes it refer to the other object.
	// Be careful not to splice an error into itself, that would create reference loops!
	void splice(const std::shared_ptr<ErrorMsg> &other);

	// Wrap this error into the other one.
	// It's an operation symmetrical to splicing, only with the reverse argument order.
	// The contents of this reference will be replaced by the other object,
	// which in turn will contain the object formerly held here.
	void wrap(const std::shared_ptr<ErrorMsg> &other)
	{
		Erref dup = other;
		dup.splice(*this);
		*this = dup;
	}

	// Append the other error object at the end of the current chain.
	// If this object is NULL, it will refer to the othe robject.
	void append(const std::shared_ptr<ErrorMsg> &other);

	// Make a copy of the object in this reference, with its whole chain.
	Erref copy();

	// If this reference contains an error, print the message from it
	// on stdout and exit(1).
	void printAndExitOnError();
};

// A high-level way to report the variety of user-defined errors,
// combining both the detailed reporting of the reasons in strings
// and the reasonably easy computational checks by error codes.
class ErrorMsg
{
public:
	// The definition for the simple error sources that don't
	// differentiate between the error codes but always use
	// the code OTHER for all their error reports.
	enum CommonErrors {
		// The code 0 is reserved for ERROR_SUCCESS
		OTHER = 1
	};

	// The source defines the namespace for the error codes.
	// Each library/class/whatever defines its own static source object.
	// of FormatMessage() source.
	class Source
	{
	public:
		Source(
			__in const wchar_t *name,
			__in_opt const GUID *guid,
			__in uint32_t testFlags = 0
		) :
			name_(name), guid_(guid), muiModule_(NULL), testFlags_(testFlags)
		{ 

		}

		enum TestFlags {
			STF_INIT_ERR1 = 0x00000001, // simulate an error in initialization
		};

		const wchar_t *name_; // Name of the source, which can be used to
			// convert the message to a pure-string, for reporting
			// up to the layers that don't have the knowledge of
			// the details of the lower levels.
			// Normally the name must point to a static string.
		const GUID *guid_; // Pointer to a static location with GUID,
			// will be NULL if this source has no GUID. A GUID is
			// needed only of the library wants to be an ETW provider
			// on its own.
		HMODULE muiModule_; // Handle of the MUI module with the
			// messages. If this source does not use MUI, the
			// handle will be NULL.
			// The handle is defined in the base class to let the
			// general functions find out whether this is a MUI
			// source or not.
		uint32_t testFlags_; // Enable the special handling that allows
			// to exercise the otherwise practically unreachable code.
		Erref testError_; // A special way to save the
			// test-generated error when otherwise the code would be aborted.

		// Wrappers around the ErrorMsg static methods to avoid specifying the class hierarchies twice.
		std::shared_ptr<ErrorMsg> __cdecl mkString(
			__in DWORD code,
			_In_z_ _Printf_format_string_ const WCHAR *fmt,
			...);
		std::shared_ptr<ErrorMsg> __cdecl mkSystem(
			__in DWORD sysCode,
			__in DWORD appCode,
			_In_z_ _Printf_format_string_ const WCHAR *fmt,
			...);

	private:
		Source();
	};

	// The source for internal errors.
	class InternalErrorSource : public Source
	{
	public:
		InternalErrorSource(
			__in const wchar_t *name
		) :
			Source(name, NULL)
		{

		}

		enum Errors {
			MODULE_LOAD_FAILURE = OTHER + 1, // A source's module failed to load.
			NO_MUI_HANDLE, // mkMui() was used on a source that doesn't support MUI.
			MUST_USE_MUI, // A plain mkString() or such was used on a MUI source.
			MUI_NO_MESSAGE, // Found no message for the requested error code.
		};
	};


	// A source that uses MUI for the localized strings.
	// Define a static object of this type, and it will
	// automatically open the MUI handle in the constructor
	// and close it in the destructor.
	class MuiSource : public Source
	{
	public:
		// Open a MUI handle on construction and close on destruction.
		// The name and guid arguments must point to static memory,
		// since these pointers will be kept in the object as-is.
		//ErrorMsg::MuiSource::MuiSource(
        MuiSource(
			__in const wchar_t *name,
			__in_opt const GUID *guid,
			__in uint32_t testFlags = 0
		);
		~MuiSource();

		// Wrappers around the ErrorMsg static methods to avoid specifying the class hierarchies twice.
		std::shared_ptr<ErrorMsg> __cdecl mkMui(
			__in DWORD code,
			... // Arguments for the format specified in MUI
		);
		std::shared_ptr<ErrorMsg> __cdecl mkMuiSystem(
			__in DWORD sysCode,
			__in DWORD appCode,
			...);

	private:
		MuiSource();
		MuiSource(const MuiSource &);
		void operator=(const MuiSource &);

		// These must not be used with a MUI source.
		std::shared_ptr<ErrorMsg> __cdecl mkString(
			__in DWORD code,
			_In_z_ _Printf_format_string_ const WCHAR *fmt,
			...);
		std::shared_ptr<ErrorMsg> __cdecl mkSystem(
			__in DWORD sysCode,
			__in DWORD appCode,
			_In_z_ _Printf_format_string_ const WCHAR *fmt,
			...);
	};

public:
	virtual ~ErrorMsg(); // allows extension

	// The constructed objects MUST ALWAYS be wrapped in an std::shared_ptr
	// and only then can be assigned somewhere. For example:
	// err->chain_ = std::make_shared<ErrorMsg>(source, code);
	// err->chain_ = std::shared_ptr<ErrorMsg>(new ErrorMsg(source, code));

	ErrorMsg() :
		source_(NULL), code_(ERROR_SUCCESS)
	{

	}

	ErrorMsg(
		__in const Source *source,
		__in DWORD code
	) :
		source_(source), code_(code)
	{
		
	}

	// Copies the whole chain, completely separating from the original.
	ErrorMsg(const ErrorMsg &orig);

	// Construct by making a copy. Copies the whole chain.
	static std::shared_ptr<ErrorMsg> mkCopy(const ErrorMsg &orig);
	static std::shared_ptr<ErrorMsg> mkCopy(const Erref &orig);

	// Construct by printing an arbitrary non-localized message.
	// source - identity of the source
	// code - the error code
	// fmt - the printf-like format string
	// Returns the new ErrorMsg object.
	static std::shared_ptr<ErrorMsg> __cdecl mkString(
		__in const Source *source,
		__in DWORD code,
		_In_z_ _Printf_format_string_ const WCHAR *fmt,
		...);
	// Construct by printing an arbitrary non-localized message with variable args.
	// source - identity of the source
	// code - the error code
	// fmt - the printf-like format string
	// args - the printf-like variable arguments
	// Returns the new ErrorMsg object.
	static std::shared_ptr<ErrorMsg> __cdecl mkStringVa(
		__in const Source *source,
		__in DWORD code,
		_In_z_ const WCHAR *fmt,
		__in va_list args
	);

	// Construct by printing the explanation of a system error.
	// code - the NT error code
	// Returns the new ErrorMsg object.
	static std::shared_ptr<ErrorMsg> mkSystem(
		__in DWORD code
	);

	// Construct by printing the explanation of a stdio error.
	// code - the stdio error code
	// Returns the new ErrorMsg object.
	static std::shared_ptr<ErrorMsg> mkErrno(
		__in DWORD code // not a Windows arror but an errno!
	);
	// Calls _get_errno() to get the value.
	static std::shared_ptr<ErrorMsg> mkErrno();

	// Construct an error by printing a non-localized error message,
	// and chaining another error with the system error message
	// that caused the application-level error.
	// sysCode - code of the underlying system error
	// source - identity of the source of the application error
	// appCode - the application-level error code
	// fmt - the printf-like format string
	// Returns the new ErrorMsg object.
	static std::shared_ptr<ErrorMsg> __cdecl mkSystem(
		__in DWORD sysCode,
		__in const Source *source,
		__in DWORD appCode,
		_In_z_ _Printf_format_string_ const WCHAR *fmt,
		...);
	// sysCode - code of the underlying system error
	// source - identity of the source of the application error
	// appCode - the application-level error code
	// fmt - the printf-like format string
	// args - the printf-like variable arguments
	// Returns the new ErrorMsg object.
	static std::shared_ptr<ErrorMsg> __cdecl mkSystemVa(
		__in DWORD sysCode,
		__in const Source *source,
		__in DWORD appCode,
		_In_z_ const WCHAR *fmt,
		__in va_list args
	);

	// Constuct an error by printing a MUI-localized error message.
	// source - identity of the source
	// code - the error code
	// ... - arguments for the format string specified in MUI
	// Returns the new ErrorMsg object.
	static std::shared_ptr<ErrorMsg> __cdecl mkMui(
		__in const Source *source, // must be a MUI source
		__in DWORD code,
		... // Arguments for the format specified in MUI
	);
	// source - identity of the source
	// code - the error code
	// args - variable arguments for the format string specified in MUI
	// Returns the new ErrorMsg object.
	static std::shared_ptr<ErrorMsg> __cdecl mkMuiVa(
		__in const Source *source, // must be a MUI source
		__in DWORD code,
		__in va_list args // Arguments for the format specified in MUI
	);

	// Construct a MUI error originating from a system error.
	// The system message gets chained to the MUI message.
	// sysCode - code of the underlying system error
	// source - identity of the source of the application error
	// appCode - the application-level error code
	// ... - arguments for the format string specified in MUI
	// Returns the new ErrorMsg object.
	static std::shared_ptr<ErrorMsg> __cdecl mkMuiSystem(
		__in DWORD sysCode,
		__in const Source *source,
		__in DWORD appCode,
		...);
	// sysCode - code of the underlying system error
	// source - identity of the source of the application error
	// appCode - the application-level error code
	// args - arguments for the format string specified in MUI
	// Returns the new ErrorMsg object.
	static std::shared_ptr<ErrorMsg> __cdecl mkMuiSystemVa(
		__in DWORD sysCode,
		__in const Source *source,
		__in DWORD appCode,
		__in va_list args
	);

	// Convert the whole error chain to a single printable string.
	std::wstring toString();

	// Convert the error chain to a printable string, up to the size limit.
	// The rest of the chain that didn't fit within the limit will be returned
	// and can be used for the next call, to get the next chunk.
	// The break is always done on the message boundary. At least one
	// message is always included, it might be longer than the limit.
	// 
	// limit - the size limit for the resulting string, in characters
	//      (not bytes, if in Unicode then twice more bytes will be used)
	// next - place to return the continuation (will point into the middle
	//      of the original chain); if everything is converted then
	//      next will be set to NULL
	// Returns the converted string.
	std::wstring toLimitedString(
		__in size_t limit,
		__out Erref &next);

public:
	const Source *source_; // The source of this error. NULL means "Windows NT errors."
	DWORD code_; // The error code, convenient for the machine checking.
	std::wstring msg_; // The error message in a human-readable format.
	std::shared_ptr<ErrorMsg> chain_; // The chained error, or NULL.
};

// The stdio functions return their error codes as the C standard library errno,
// with values different from the Windows errors. So here we go,
// the special source for Errno messages.
extern ErrorMsg::Source ErrnoSource;

///////////////////////// Erref methods /////////////////////////////////////////

// Check whether there is an error referenced.
inline bool Erref::hasError()
{
	ErrorMsg *msg = get();
	return (msg != NULL && msg->code_ != ERROR_SUCCESS);
}

// Get the error code, if any (or return ERROR_SUCCESS on on error).
inline DWORD Erref::getCode()
{
	ErrorMsg *msg = get();
	if (msg == NULL)
		return ERROR_SUCCESS; // no error
	return msg->code_;
}

// Get the chained error. Returns NULL if either the chained error
// is not present, or this reference is NULL.
inline Erref Erref::getChain()
{
	ErrorMsg *msg = get();
	if (msg == NULL)
		return (Erref)0; // no error
	return msg->chain_;
}

// Get the error code of the chained error, if any (or return ERROR_SUCCESS on on error).
inline DWORD Erref::getChainCode()
{
	ErrorMsg *msg = get();
	if (msg == NULL)
		return ERROR_SUCCESS; // no error
	msg = msg->chain_.get();
	if (msg == NULL)
		return ERROR_SUCCESS; // no error
	return msg->code_;
}

