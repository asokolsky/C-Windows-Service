#pragma once

// TODO: add headers that you want to pre-compile here
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winnt.h>

#include <evntprov.h>
#include <synchapi.h>
#include <muiload.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <memory>
#include <deque>

#include "../Critical.hpp"
#include "../ErrorHelpers.hpp"
#include "../Logger.hpp"
// TODO: reference additional headers your program requires here
#include "../Service.hpp"


