#ifndef VERSIONHELPER_H_INCLUDED
#define VERSIONHELPER_H_INCLUDED

#define VERSIONHELPERAPI inline bool

#define _WIN32_WINNT_NT4            0x0400
#define _WIN32_WINNT_WIN2K          0x0500
#define _WIN32_WINNT_WINXP          0x0501
#define _WIN32_WINNT_WS03           0x0502
#define _WIN32_WINNT_WIN6           0x0600
#define _WIN32_WINNT_VISTA          0x0600
#define _WIN32_WINNT_WS08           0x0600
#define _WIN32_WINNT_LONGHORN       0x0600
#define _WIN32_WINNT_WIN7           0x0601
#define _WIN32_WINNT_WIN8           0x0602
#define _WIN32_WINNT_WINBLUE        0x0603
#define _WIN32_WINNT_WIN10          0x0A00        

typedef LONG(NTAPI *fnRtlGetVersion)(PRTL_OSVERSIONINFOEXW lpVersionInformation);

enum eVerShort
{
	WinUnsupported, // Unsupported OS 
	WinXP,          // Windows XP
	Win7,           // Windows 7
	Win8,           // Windows 8
	Win8Point1,     // Windows 8.1
	Win10,          // Windows 10
	Win10AU,        // Windows 10 Anniversary update
	Win10CU         // Windows 10 Creators update
};

struct WinVersion
{
	eVerShort ver = WinUnsupported;
	RTL_OSVERSIONINFOEXW native;
};

inline WinVersion& WinVer()
{
	static WinVersion g_WinVer;
	return g_WinVer;
}

inline void InitVersion()
{
	auto& g_WinVer = WinVer();
	g_WinVer.native.dwOSVersionInfoSize = sizeof(g_WinVer.native);
	auto RtlGetVersion = (fnRtlGetVersion)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
	if (RtlGetVersion)
		RtlGetVersion(&g_WinVer.native);

	if (g_WinVer.native.dwMajorVersion != 0)
	{
		auto fullver = (g_WinVer.native.dwMajorVersion << 8) | g_WinVer.native.dwMinorVersion;
		switch (fullver)
		{
		case _WIN32_WINNT_WIN10:
			if (g_WinVer.native.dwBuildNumber >= 15063)
				g_WinVer.ver = Win10CU;
			else if (g_WinVer.native.dwBuildNumber >= 14393)
				g_WinVer.ver = Win10AU;
			else if (g_WinVer.native.dwBuildNumber >= 10586)
				g_WinVer.ver = Win10;
			break;

		case _WIN32_WINNT_WINBLUE:
			g_WinVer.ver = Win8Point1;
			break;

		case _WIN32_WINNT_WIN8:
			g_WinVer.ver = Win8;
			break;

		case _WIN32_WINNT_WIN7:
			g_WinVer.ver = Win7;
			break;

		case _WIN32_WINNT_WINXP:
			g_WinVer.ver = WinXP;
			break;

		default:
			g_WinVer.ver = WinUnsupported;
		}
	}
}


VERSIONHELPERAPI
IsWindowsVersionOrGreater(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor, DWORD dwBuild)
{
	auto& g_WinVer = WinVer();
	if (g_WinVer.native.dwMajorVersion != 0)
	{
		if (g_WinVer.native.dwMajorVersion > wMajorVersion)
			return true;
		else if (g_WinVer.native.dwMajorVersion < wMajorVersion)
			return false;

		if (g_WinVer.native.dwMinorVersion > wMinorVersion)
			return true;
		else if (g_WinVer.native.dwMinorVersion < wMinorVersion)
			return false;

		if (g_WinVer.native.wServicePackMajor > wServicePackMajor)
			return true;
		else if (g_WinVer.native.wServicePackMajor < wServicePackMajor)
			return false;

		if (g_WinVer.native.dwBuildNumber >= dwBuild)
			return true;
	}

	return false;
}

VERSIONHELPERAPI
IsWindowsXPOrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WINXP), LOBYTE(_WIN32_WINNT_WINXP), 0, 0);
}

VERSIONHELPERAPI
IsWindowsXPSP1OrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WINXP), LOBYTE(_WIN32_WINNT_WINXP), 1, 0);
}

VERSIONHELPERAPI
IsWindowsXPSP2OrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WINXP), LOBYTE(_WIN32_WINNT_WINXP), 2, 0);
}

VERSIONHELPERAPI
IsWindowsXPSP3OrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WINXP), LOBYTE(_WIN32_WINNT_WINXP), 3, 0);
}

VERSIONHELPERAPI
IsWindowsVistaOrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_VISTA), LOBYTE(_WIN32_WINNT_VISTA), 0, 0);
}

VERSIONHELPERAPI
IsWindowsVistaSP1OrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_VISTA), LOBYTE(_WIN32_WINNT_VISTA), 1, 0);
}

VERSIONHELPERAPI
IsWindowsVistaSP2OrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_VISTA), LOBYTE(_WIN32_WINNT_VISTA), 2, 0);
}

VERSIONHELPERAPI
IsWindows7OrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WIN7), LOBYTE(_WIN32_WINNT_WIN7), 0, 0);
}

VERSIONHELPERAPI
IsWindows7SP1OrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WIN7), LOBYTE(_WIN32_WINNT_WIN7), 1, 0);
}

VERSIONHELPERAPI
IsWindows8OrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WIN8), LOBYTE(_WIN32_WINNT_WIN8), 0, 0);
}

VERSIONHELPERAPI
IsWindows8Point1OrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WINBLUE), LOBYTE(_WIN32_WINNT_WINBLUE), 0, 0);
}

VERSIONHELPERAPI
IsWindows10OrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WIN10), LOBYTE(_WIN32_WINNT_WIN10), 0, 0);
}

VERSIONHELPERAPI
IsWindows10AnniversaryOrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WIN10), LOBYTE(_WIN32_WINNT_WIN10), 0, 14393);
}

VERSIONHELPERAPI
IsWindows10CreatorsOrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WIN10), LOBYTE(_WIN32_WINNT_WIN10), 0, 15063);
}

VERSIONHELPERAPI
IsWindowsServer()
{
	OSVERSIONINFOEXW osvi = { sizeof(osvi), 0, 0, 0, 0,{ 0 }, 0, 0, 0, VER_NT_WORKSTATION, 0 };
	DWORDLONG        const dwlConditionMask = VerSetConditionMask(0, VER_PRODUCT_TYPE, VER_EQUAL);

	return !VerifyVersionInfoW(&osvi, VER_PRODUCT_TYPE, dwlConditionMask);
}

#endif