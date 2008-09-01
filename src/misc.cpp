/*
  Glaurung, a UCI chess playing engine.
  Copyright (C) 2004-2008 Tord Romstad

  Glaurung is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  Glaurung is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


////
//// Includes
////

#if !defined(_MSC_VER)

#  include <sys/time.h>
#  include <sys/types.h>
#  include <unistd.h>

#else

#  include <windows.h>
#  include <time.h>
#  include "dos.h"
int gettimeofday(struct timeval * tp, struct timezone * tzp);

#endif

#include <cstdio>
#include <iomanip>
#include <sstream>

#include "misc.h"


//// 
//// Functions
////

/// engine_name() returns the full name of the current Glaurung version.
/// This will be either "Glaurung YYMMDD" (where YYMMDD is the date when the
/// program was compiled) or "Glaurung <version number>", depending on whether
/// the constant EngineVersion (defined in misc.h) is empty.

const std::string engine_name() {
  if(EngineVersion == "") {
    static const char monthNames[12][4] = {
      "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
    };
    const char *dateString = __DATE__;
    std::stringstream s;
    int month = 0, day = 0;
    
    for(int i = 0; i < 12; i++)
      if(strncmp(dateString, monthNames[i], 3) == 0)
        month = i + 1;
    day = atoi(dateString+4);
    
    s << "Glaurung " << (dateString+9) << std::setfill('0') << std::setw(2)
      << month << std::setfill('0') << std::setw(2) << day;
    
    return s.str();
  }
  else
    return "Glaurung " + EngineVersion;
}


/// get_system_time() returns the current system time, measured in
/// milliseconds.

int get_system_time() {
  struct timeval t;
  gettimeofday(&t, NULL);
  return t.tv_sec*1000 + t.tv_usec/1000; 
}


/// cpu_count() tries to detect the number of CPU cores.

#if !defined(_MSC_VER)

#  if defined(_SC_NPROCESSORS_ONLN)
int cpu_count() {
  return Min(sysconf(_SC_NPROCESSORS_ONLN), 8);
}
#  else
int cpu_count() {
  return 1;
}
#  endif

#else

int cpu_count() {
  SYSTEM_INFO s;
  GetSystemInfo(&s);
  return Min(s.dwNumberOfProcessors, 8);
}

#endif


/*
  From Beowulf, from Olithink
*/
#ifndef _WIN32
/* Non-windows version */
int Bioskey()
{
  fd_set          readfds;
  struct timeval  timeout;
  
  FD_ZERO(&readfds);
  FD_SET(fileno(stdin), &readfds);
  /* Set to timeout immediately */
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  select(16, &readfds, 0, 0, &timeout);
  
  return (FD_ISSET(fileno(stdin), &readfds));
}

#else
/* Windows-version */
#include <windows.h>
#include <conio.h>
int Bioskey()
{
    static int      init = 0,
                    pipe;
    static HANDLE   inh;
    DWORD           dw;
    /* If we're running under XBoard then we can't use _kbhit() as the input
     * commands are sent to us directly over the internal pipe */

#if defined(FILE_CNT)
    if (stdin->_cnt > 0)
        return stdin->_cnt;
#endif
    if (!init) {
        init = 1;
        inh = GetStdHandle(STD_INPUT_HANDLE);
        pipe = !GetConsoleMode(inh, &dw);
        if (!pipe) {
            SetConsoleMode(inh, dw & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT));
            FlushConsoleInputBuffer(inh);
        }
    }
    if (pipe) {
        if (!PeekNamedPipe(inh, NULL, 0, NULL, &dw, NULL))
            return 1;
        return dw;
    } else {
        GetNumberOfConsoleInputEvents(inh, &dw);
        return dw <= 1 ? 0 : dw;
    }
}
#endif
