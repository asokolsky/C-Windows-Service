#pragma once

class Critical
{
public:
	Critical()
	{
		InitializeCriticalSection(&cs_);
	}
	~Critical()
	{
		DeleteCriticalSection(&cs_);
	}

	void enter()
	{
		EnterCriticalSection(&cs_);
	}

	void leave()
	{
		LeaveCriticalSection(&cs_);
	}

public:
	CRITICAL_SECTION cs_;
};

// a scoped enter-leave
class ScopeCritical
{
public:
	ScopeCritical(Critical &cr) :
		cr_(cr)
	{
		cr_.enter();
	}

	~ScopeCritical()
	{
		cr_.leave();
	}

protected:
	Critical &cr_;

private:
	ScopeCritical();
	ScopeCritical(const ScopeCritical &);
	void operator=(const ScopeCritical &);
};
