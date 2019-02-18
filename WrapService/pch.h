// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file

#ifndef PCH_H
#define PCH_H

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


#endif //PCH_H
