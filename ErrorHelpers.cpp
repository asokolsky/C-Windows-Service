#include "pch.h"
//#include "ErrorHelpers.hpp"
//#include <stdarg.h>

//-------------------------------- ErrorHelpers.cpp ---------------------------

//////////////// general helper functions /////////////////////////////

std::wstring __cdecl wstrprintf(
	_In_z_ _Printf_format_string_ const WCHAR *fmt,
	...)
{
	std::wstring res;
	va_list args;
	va_start(args, fmt);
	vwstrAppendF(res, fmt, args);
	va_end(args);
	return res;
}

std::wstring __cdecl vwstrprintf(
	_In_z_ const WCHAR *fmt,
	__in va_list args)
{
	std::wstring res;
	vwstrAppendF(res, fmt, args);
	return res;
}

void __cdecl wstrAppendF(
	__inout std::wstring &dest,
	_In_z_ _Printf_format_string_ const WCHAR *fmt,
	...
)
{
	va_list args;
	va_start(args, fmt);
	vwstrAppendF(dest, fmt, args);
	va_end(args);
}

void __cdecl vwstrAppendF(
	__inout std::wstring &dest,
	_In_z_ const WCHAR *fmt,
	__in va_list args)
{
	va_list cpargs;

	cpargs = args; // in standard form would be va_copy(cpargs, args);
	int reqlen = _vscwprintf(fmt, cpargs) + 1; // +1 for \0
	va_end(cpargs);

	PWCHAR buf = new WCHAR[reqlen];
	int result = _vsnwprintf_s(buf, reqlen, _TRUNCATE, fmt, args);

	if (result < 0) 
	{ // should never happen
		dest.append(L"[vwstrAppendF: Internal error: could not compute the required string length]");
	}
	else 
	{
		dest.append(buf);
	}

	delete[] buf;
}

////////////////////// ErrorMsg::InternalErrorSource //////////////////
// The Source for the internal errors.
static WCHAR internalErrorSourceName[] = L"ErrorMsg";
static ErrorMsg::InternalErrorSource internalErrorSource(internalErrorSourceName);

////////////////////// Erref //////////////////////////////////////////

void Erref::splice(const std::shared_ptr<ErrorMsg> &other)
{
	if (!other)
		return;
	if (!*this) 
	{
		*this = other;
		return;
	}

	ErrorMsg *msg = get();
	if (!msg->chain_) 
	{
		msg->chain_ = other;
		return;
	}

	Erref oldChain = msg->chain_;
	msg->chain_ = other;

	Erref eit = other;
	for (; eit->chain_; eit = eit->chain_) 
	{
		;
	}
	eit->chain_ = oldChain;
}

void Erref::append(const std::shared_ptr<ErrorMsg> &other)
{
	if (!other)
		return;
	if (!*this) 
	{
		*this = other;
		return;
	}

	Erref eit = *this;
	for (; eit->chain_; eit = eit->chain_) 
	{
	}
	eit->chain_ = other;
}

void Erref::printAndExitOnError()
{
	if (hasError()) 
	{
		wstring s = get()->toString();
		wprintf(L"%ls", s.c_str());
		exit(1);
	}
}

Erref Erref::copy()
{
	return ErrorMsg::mkCopy(*this);
}

////////////////////// ErrorMsg ///////////////////////////////////////

ErrorMsg::Source ErrnoSource(L"Errno", NULL);

ErrorMsg::ErrorMsg(const ErrorMsg &orig) :
	source_(orig.source_), code_(orig.code_), msg_(orig.msg_)
{
	if (orig.chain_)
		chain_ = mkCopy(orig.chain_);
}

ErrorMsg::~ErrorMsg()
{ 
}

std::shared_ptr<ErrorMsg> ErrorMsg::mkCopy(const ErrorMsg &orig)
{
	return make_shared<ErrorMsg>(orig);
}

std::shared_ptr<ErrorMsg> ErrorMsg::mkCopy(const Erref &orig)
{
	if (orig)
		return make_shared<ErrorMsg>(*orig.get());
	return NULL;
}

shared_ptr<ErrorMsg> ErrorMsg::mkString(
	__in const Source *source,
	__in DWORD code,
	_In_z_ _Printf_format_string_ const WCHAR *fmt,
	...)
{
	va_list args;
	va_start(args, fmt);
	std::shared_ptr<ErrorMsg> err = mkStringVa(source, code, fmt, args);
	va_end(args);
	return err;
}

shared_ptr<ErrorMsg> ErrorMsg::mkStringVa(
	__in const Source *source,
	__in DWORD code,
	_In_z_ const WCHAR *fmt,
	__in va_list args
)
{
	Erref err = make_shared<ErrorMsg>(source, code);
	err->msg_ = vwstrprintf(fmt, args);

	// if the message includes \r\n, drop them
	size_t msize = err->msg_.size();
	while (msize > 0
		&& (err->msg_[msize - 1] == L'\n' || err->msg_[msize - 1] == L'\r')) 
	{
		--msize;
		err->msg_.resize(msize);
	}

	if (source->muiModule_ != NULL) 
	{
		// Return at least the original error code but chain the internal error message
		err.splice(internalErrorSource.mkString(
			InternalErrorSource::MUST_USE_MUI,
			L"Internal error: used a plain-text error on the MUIsource '%ls'.",
			source->name_));
	}
	return err;
}

shared_ptr<ErrorMsg> ErrorMsg::mkSystem(
	__in DWORD code
)
{
	std::shared_ptr<ErrorMsg> err = make_shared<ErrorMsg>((Source *)NULL, code);
	LPWSTR buf = NULL;
	FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&buf,
		0, NULL);
	if (buf) 
	{
		err->msg_ = buf;
	}
	else 
	{
		err->msg_ = L"[error text not found]";
	}

	// for whatever reason, the system message tends to include \r\n
	size_t msize = err->msg_.size();
	while (msize > 0
		&& (err->msg_[msize - 1] == L'\n' || err->msg_[msize - 1] == L'\r')) 
	{
		--msize;
		err->msg_.resize(msize);
	}
	LocalFree(buf);
	return err;
}

shared_ptr<ErrorMsg> ErrorMsg::mkErrno(
	__in DWORD code
)
{
	std::shared_ptr<ErrorMsg> err = make_shared<ErrorMsg>(&ErrnoSource, code);
	err->msg_ = _wcserror(code);
	return err;
}

shared_ptr<ErrorMsg> ErrorMsg::mkErrno()
{
	int eval;
	_get_errno(&eval);
	return mkErrno(eval);
}

shared_ptr<ErrorMsg> ErrorMsg::mkSystem(
	__in DWORD sysCode,
	__in const Source *source,
	__in DWORD appCode,
	_In_z_ _Printf_format_string_ const WCHAR *fmt,
	...)
{
	va_list args;
	va_start(args, fmt);
	std::shared_ptr<ErrorMsg> err = mkSystemVa(sysCode, source, appCode, fmt, args);
	va_end(args);
	return err;
}

shared_ptr<ErrorMsg> ErrorMsg::mkSystemVa(
	__in DWORD sysCode,
	__in const Source *source,
	__in DWORD appCode,
	_In_z_ const WCHAR *fmt,
	__in va_list args
)
{
	Erref err = make_shared<ErrorMsg>(source, appCode);
	err->msg_ = vwstrprintf(fmt, args);
	err->chain_ = mkSystem(sysCode);
	if (source->muiModule_ != NULL) 
	{
		// Return at least the original error code but chain the internal error message
		err.splice(internalErrorSource.mkString(
			InternalErrorSource::MUST_USE_MUI,
			L"Internal error: used a plain-text error on the MUIsource '%ls'.",
			source->name_));
	}
	return err;
}

shared_ptr<ErrorMsg> ErrorMsg::mkMui(
	__in const Source *source,
	__in DWORD code,
	...
)
{
	va_list args;
	va_start(args, code);
	std::shared_ptr<ErrorMsg> err = mkMuiVa(source, code, args);
	va_end(args);
	return err;
}

shared_ptr<ErrorMsg> ErrorMsg::mkMuiVa(
	__in const Source *source,
	__in DWORD code,
	__in va_list args
)
{
	Erref err = make_shared<ErrorMsg>(source, code);

	if (source->muiModule_ == NULL) 
	{
		// Return at least the original error code but chain the internal error message
		err.splice(internalErrorSource.mkString(
			InternalErrorSource::NO_MUI_HANDLE,
			L"Internal error: attempted to use a MUI error reporting on the source '%ls' without MUI support.",
			source->name_));
	}
	else 
	{
		LPWSTR buf = NULL;
		DWORD res = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER
			| FORMAT_MESSAGE_FROM_HMODULE,
			source->muiModule_,
			code,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPWSTR)&buf,
			0, &args);
		if (res == 0) 
		{
			err.splice(internalErrorSource.mkSystem(GetLastError(),
				InternalErrorSource::MUI_NO_MESSAGE,
				L"Internal error: cannot find a MUI message for source '%ls' code 0x%x.",
				source->name_, code));
		}
		if (buf != NULL) 
		{
			err->msg_ = buf;

			// for whatever reason, the message always includes \r\n
			size_t msize = err->msg_.size();
			while (msize > 0
				&& (err->msg_[msize - 1] == L'\n' || err->msg_[msize - 1] == L'\r')) 
			{
				--msize;
				err->msg_.resize(msize);
			}

			LocalFree(buf);
		}
	}

	return err;
}

std::shared_ptr<ErrorMsg> ErrorMsg::mkMuiSystem(
	__in DWORD sysCode,
	__in const Source *source,
	__in DWORD appCode,
	...)
{
	va_list args;
	va_start(args, appCode);
	std::shared_ptr<ErrorMsg> err = mkMuiSystemVa(sysCode, source, appCode, args);
	va_end(args);
	return err;
}

std::shared_ptr<ErrorMsg> ErrorMsg::mkMuiSystemVa(
	__in DWORD sysCode,
	__in const Source *source,
	__in DWORD appCode,
	__in va_list args
)
{
	Erref err = mkMuiVa(source, appCode, args);
	err.splice(mkSystem(sysCode));
	return err;
}

std::wstring ErrorMsg::toString()
{
	std::wstring res;
	bool outer = true;
	ErrorMsg *err;
	size_t estimate;

	// first estimate the required string size, to avoid reallocating it continuously
	estimate = 0;
	for (err = this; err != NULL; err = err->chain_.get()) 
	{
		estimate += 40; // for the source name and error number, and better estimate high
		estimate += err->msg_.size();
	}
	res.reserve(estimate);

	// now print the messages
	for (err = this; err != NULL; err = err->chain_.get())
	{
		if (!outer) 
		{
			res.append(L"  "); // indent the dependent errors
		}
		else
		{
			outer = false;
		}
		if (err->source_ == NULL)
		{
			res.append(L"NT");
		}
		else
		{
			res.append(err->source_->name_);
		}
		res.append(wstrprintf(L":%d:0x%x: ", (err->code_ & 0x3FFFFFFF), err->code_));
		res.append(err->msg_); // if contains \n, would not follow the indenting nicely
		if (res[res.size() - 1] != L'\n')
			res.append(L"\n");
	}

	return res;
}

std::wstring ErrorMsg::toLimitedString(
	__in size_t limit,
	__out Erref &next)
{
	std::wstring res;
	bool outer = true;
	ErrorMsg *err;
	size_t estimate;

	// first estimate the required string size, to avoid reallocating it continuously
	estimate = 0;
	for (err = this; err != NULL; err = err->chain_.get())
	{
		estimate += 40; // for the source name and error number, and better estimate high
		estimate += err->msg_.size();
		if (estimate > limit)
			break;
	}
	res.reserve(estimate);

	// now print the messages
	ErrorMsg *lasterr = NULL;
	for (err = this; err != NULL; lasterr = err, err = err->chain_.get())
	{
		size_t prevsz = res.size();

		if (!outer)
		{
			res.append(L"  "); // indent the dependent errors
		}
		else
		{
			outer = false;
		}
		if (err->source_ == NULL)
		{
			res.append(L"NT");
		}
		else
		{
			res.append(err->source_->name_);
		}
		res.append(wstrprintf(L":%d:0x%x: ", (err->code_ & 0x3FFFFFFF), err->code_));

		if (res.size() + err->msg_.size() >= limit && lasterr)
		{
			res.resize(prevsz); // throw away the partial last message
			next = lasterr->chain_; // continue from this point on the next call
			return res;
		}

		res.append(err->msg_); // if contains \n, would not follow the indenting nicely
		if (res[res.size() - 1] != L'\n')
			res.append(L"\n");
	}

	next.reset();
	return res;
}
////////////////////// ErrorMsg::Source ///////////////////////////////

shared_ptr<ErrorMsg> ErrorMsg::Source::mkString(
	__in DWORD code,
	_In_z_ _Printf_format_string_ const WCHAR *fmt,
	...)
{
	va_list args;
	va_start(args, fmt);
	std::shared_ptr<ErrorMsg> err = ErrorMsg::mkStringVa(this, code, fmt, args);
	va_end(args);
	return err;
}

shared_ptr<ErrorMsg> ErrorMsg::Source::mkSystem(
	__in DWORD sysCode,
	__in DWORD appCode,
	_In_z_ _Printf_format_string_ const WCHAR *fmt,
	...)
{
	va_list args;
	va_start(args, fmt);
	std::shared_ptr<ErrorMsg> err = ErrorMsg::mkSystemVa(sysCode, this, appCode, fmt, args);
	va_end(args);
	return err;
}

////////////////////// ErrorMsg::MuiSource ////////////////////////////

ErrorMsg::MuiSource::MuiSource(
	__in const wchar_t *name,
	__in_opt const GUID *guid,
	__in uint32_t testFlags
) :
	Source(name, guid, testFlags)
{
	HMODULE m = NULL;

	// Normally this would be static, located inside the module, so it
	// can be used to find the module itself.
	LPCWSTR anchor = (LPCWSTR)this;
	if (testFlags_ & STF_INIT_ERR1)
		anchor = NULL;

	do
	{
		// This logic can handle the situations
		// when this object is defined in either a DLL or an EXE.
		if (!GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
			| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			anchor,
			&m))
		{
			testError_ = internalErrorSource.mkSystem(GetLastError(),
				InternalErrorSource::MODULE_LOAD_FAILURE,
				L"Internal error: failed to load the MUI module for MuiSource '%ls' at address %p.",
				name, anchor);
			break;
		}

		muiModule_ = m;
#pragma warning(suppress: 4127) // constant conditional expression
	} while (0);

	if (!(testFlags_ & STF_INIT_ERR1)
		&& testError_.hasError())
	{
		wstring msg = testError_->toString();
		// Print the error everywhere we can reach
		wprintf(L"%s", msg.c_str()); fflush(stdout);
		fwprintf(stderr, L"%s", msg.c_str()); fflush(stderr);
		abort();
	}
}

ErrorMsg::MuiSource::~MuiSource()
{
	// Because of GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT on opening
	// the handle, no need to call FreeLibrary().
	muiModule_ = NULL;
}

shared_ptr<ErrorMsg> ErrorMsg::MuiSource::mkMui(
	__in DWORD code,
	...)
{
	va_list args;
	va_start(args, code);
	std::shared_ptr<ErrorMsg> err = ErrorMsg::mkMuiVa(this, code, args);
	va_end(args);
	return err;
}

std::shared_ptr<ErrorMsg> ErrorMsg::MuiSource::mkMuiSystem(
	__in DWORD sysCode,
	__in DWORD appCode,
	...)
{
	va_list args;
	va_start(args, appCode);
	std::shared_ptr<ErrorMsg> err = ErrorMsg::mkMuiSystemVa(sysCode, this, appCode, args);
	va_end(args);
	return err;
}

