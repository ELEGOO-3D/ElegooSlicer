#include "BaseException.h"
#include <iomanip>
#include <string>
#include <sstream>
#include <iostream>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
#include <mutex>

// Forward declare Windows API functions to avoid header conflicts
#ifdef _WIN32
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")

extern "C" {
    typedef struct _PROCESS_MEMORY_COUNTERS_EX {
        DWORD cb;
        DWORD PageFaultCount;
        SIZE_T PeakWorkingSetSize;
        SIZE_T WorkingSetSize;
        SIZE_T QuotaPeakPagedPoolUsage;
        SIZE_T QuotaPagedPoolUsage;
        SIZE_T QuotaPeakNonPagedPoolUsage;
        SIZE_T QuotaNonPagedPoolUsage;
        SIZE_T PagefileUsage;
        SIZE_T PeakPagefileUsage;
        SIZE_T PrivateUsage;
    } PROCESS_MEMORY_COUNTERS_EX, *PPROCESS_MEMORY_COUNTERS_EX;

    __declspec(dllimport) BOOL __stdcall GetProcessMemoryInfo(
        HANDLE Process,
        void* ppsmemCounters,
        DWORD cb
    );
}
#endif

#ifndef ELEGOOSLICER_VERSION
#define ELEGOOSLICER_VERSION "Unknown"
#endif

static std::string g_log_folder;
static std::atomic<int> g_crash_log_count = 0;
static std::mutex g_dump_mutex;

CBaseException::CBaseException(HANDLE hProcess, WORD wPID, LPCTSTR lpSymbolPath, PEXCEPTION_POINTERS pEp):
	CStackWalker(hProcess, wPID, lpSymbolPath)
{
	if (NULL != pEp)
	{
		m_pEp = new EXCEPTION_POINTERS;
		CopyMemory(m_pEp, pEp, sizeof(EXCEPTION_POINTERS));
	}
	output_file = new boost::nowide::ofstream();
	std::time_t t = std::time(0);
	std::tm* now_time = std::localtime(&t);
	std::stringstream buf;

	if (!g_log_folder.empty()) {
		buf << std::put_time(now_time, "crash_%a_%b_%d_%H_%M_%S_") <<g_crash_log_count++ <<".log";
		auto log_folder = (boost::filesystem::path(g_log_folder) / "log").make_preferred();
		if (!boost::filesystem::exists(log_folder)) {
		    boost::filesystem::create_directory(log_folder);
	    }
		auto crash_log_path = boost::filesystem::path(log_folder / buf.str()).make_preferred();
		std::string log_filename = crash_log_path.string();
		output_file->open(log_filename, std::ios::out | std::ios::app);
	}
}

CBaseException::~CBaseException(void)
{
	if (output_file) {
		output_file->close();
		delete output_file;
	}
}


//BBS set crash log folder
void CBaseException::set_log_folder(std::string log_folder)
{
	g_log_folder = log_folder;
}

void CBaseException::OutputString(LPCTSTR lpszFormat, ...)
{
	TCHAR szBuf[2048] = _T("");
	va_list args;
	va_start(args, lpszFormat);
	_vsntprintf_s(szBuf, 2048, lpszFormat, args);
	va_end(args);

	//WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), szBuf, _tcslen(szBuf), NULL, NULL);

	//output it to the current directory of binary
	std::string output_str = textconv_helper::T2A_(szBuf);
	*output_file << output_str;
	output_file->flush();
}

void CBaseException::ShowLoadModules()
{
	LoadSymbol();
	LPMODULE_INFO pHead = GetLoadModules();
	LPMODULE_INFO pmi = pHead;

	TCHAR szBuf[MAX_COMPUTERNAME_LENGTH] = _T("");
	DWORD dwSize = MAX_COMPUTERNAME_LENGTH;
	GetUserName(szBuf, &dwSize);
	OutputString(_T("Current User:%s\r\n"), szBuf);
	OutputString(_T("BaseAddress:\tSize:\tName\tPath\tSymbolPath\tVersion\r\n"));
	while (NULL != pmi)
	{
		OutputString(_T("%08x\t%d\t%s\t%s\t%s\t%s\r\n"), (unsigned long)(pmi->ModuleAddress), pmi->dwModSize, pmi->szModuleName, pmi->szModulePath, pmi->szSymbolPath, pmi->szVersion);
		pmi = pmi->pNext;
	}

	FreeModuleInformations(pHead);
}

void CBaseException::ShowCallstack(HANDLE hThread, const CONTEXT* context)
{
	OutputString(_T("Show CallStack:\n"));
    LPSTACKINFO phead = StackWalker(hThread, context);

	// Show RVA of each call stack, so we can locate the symbol using pdb file
	// To show the symbol, load the <szFaultingModule> in WinDBG with pdb file, then type the following commands:
	// > lm                                                                   which gives you the start address of each module, as well as module names
	// > !dh <module name>                                                    list all module headers. Find the <virtual address> of the section given by
	//                                                                        the <section> output in the crash log
	// > ln <module start address> + <section virtual address> + <offset>     reveals the debug symbol
    OutputString(_T("\nLogical Address:\n"));
    TCHAR szFaultingModule[MAX_PATH];
    DWORD section, offset;
    for (LPSTACKINFO ps = phead; ps != nullptr; ps = ps->pNext) {
        if (GetLogicalAddress((PVOID) ps->szFncAddr, szFaultingModule, sizeof(szFaultingModule), section, offset)) {
            OutputString(_T("0x%X 0x%X:0x%X %s\n"), ps->szFncAddr, section, offset, szFaultingModule);
        } else {
            OutputString(_T("0x%X Unknown\n"), ps->szFncAddr);
        }
    }

	FreeStackInformations(phead);
}

void CBaseException::ShowExceptionResoult(DWORD dwExceptionCode)
{
	OutputString(_T("Exception Code :%08x "), dwExceptionCode);
// BBS: to be checked
#if 1
	switch (dwExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
		{
			//OutputString(_T("ACCESS_VIOLATION(%s)\r\n"), _T("��д�Ƿ��ڴ�"));
			OutputString(_T("ACCESS_VIOLATION\r\n"));
		}
		return ;
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		{
			//OutputString(_T("DATATYPE_MISALIGNMENT(%s)\r\n"), _T("�߳���ͼ�ڲ�֧�ֶ����Ӳ���϶�дδ���������"));
			OutputString(_T("DATATYPE_MISALIGNMENT\r\n"));
		}
		return ;
	case EXCEPTION_BREAKPOINT:
		{
			//OutputString(_T("BREAKPOINT(%s)\r\n"), _T("����һ���ϵ�"));
			OutputString(_T("BREAKPOINT\r\n"));
		}
		return ;
	case EXCEPTION_SINGLE_STEP:
		{
			//OutputString(_T("SINGLE_STEP(%s)\r\n"), _T("����")); //һ���Ƿ����ڵ����¼���
			OutputString(_T("SINGLE_STEP\r\n"));
		}
		return ;
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		{
			//OutputString(_T("ARRAY_BOUNDS_EXCEEDED(%s)\r\n"), _T("�������Խ��"));
			OutputString(_T("ARRAY_BOUNDS_EXCEEDED\r\n"));
		}
		return ;
	case EXCEPTION_FLT_DENORMAL_OPERAND:
		{
			//OutputString(_T("FLT_DENORMAL_OPERAND(%s)\r\n"), _T("���������һ�������������棬�����ĸ������޷���ʾ")); //������������
			OutputString(_T("FLT_DENORMAL_OPERAND\r\n"));
		}
		return ;
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		{
			//OutputString(_T("FLT_DIVIDE_BY_ZERO(%s)\r\n"), _T("��������0����"));
			OutputString(_T("FLT_DIVIDE_BY_ZERO\r\n"));
		}
		return ;
	case EXCEPTION_FLT_INEXACT_RESULT:
		{
			//OutputString(_T("FLT_INEXACT_RESULT(%s)\r\n"), _T("�����������Ľ���޷���ʾ")); //�޷���ʾһ��������̫С��������������ʾ�ķ�Χ, ����֮������Ľ���쳣
			OutputString(_T("FLT_INEXACT_RESULT\r\n"));
		}
		return ;
	case EXCEPTION_FLT_INVALID_OPERATION:
		{
			//OutputString(_T("FLT_INVALID_OPERATION(%s)\r\n"), _T("�����������쳣"));
			OutputString(_T("FLT_INVALID_OPERATION\r\n"));
		}
		return ;
	case EXCEPTION_FLT_OVERFLOW:
		{
			//OutputString(_T("FLT_OVERFLOW(%s)\r\n"), _T("���������ָ����������Ӧ���͵����ֵ"));
			OutputString(_T("FLT_OVERFLOW\r\n"));
		}
		return ;
	case EXCEPTION_FLT_STACK_CHECK:
		{
			//OutputString(_T("STACK_CHECK(%s)\r\n"), _T("ջԽ�����ջ�������"));
			OutputString(_T("STACK_CHECK\r\n"));
		}
		return ;
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		{
			//OutputString(_T("INT_DIVIDE_BY_ZERO(%s)\r\n"), _T("������0�쳣"));
			OutputString(_T("INT_DIVIDE_BY_ZERO\r\n"));
		}
		return ;
	case EXCEPTION_INVALID_HANDLE:
		{
			//OutputString(_T("INVALID_HANDLE(%s)\r\n"), _T("�����Ч"));
			OutputString(_T("INVALID_HANDLE\r\n"));
		}
		return ;
	case EXCEPTION_PRIV_INSTRUCTION:
		{
			//OutputString(_T("PRIV_INSTRUCTION(%s)\r\n"), _T("�߳���ͼִ�е�ǰ����ģʽ��֧�ֵ�ָ��"));
			OutputString(_T("PRIV_INSTRUCTION\r\n"));
		}
		return ;
	case EXCEPTION_IN_PAGE_ERROR:
		{
			//OutputString(_T("IN_PAGE_ERROR(%s)\r\n"), _T("�߳���ͼ����δ���ص������ڴ�ҳ���߲��ܼ��ص������ڴ�ҳ"));
			OutputString(_T("IN_PAGE_ERROR\r\n"));
		}
		return ;
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		{
			//OutputString(_T("ILLEGAL_INSTRUCTION(%s)\r\n"), _T("�߳���ͼִ����Чָ��"));
			OutputString(_T("ILLEGAL_INSTRUCTION\r\n"));
		}
		return ;
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		{
			//OutputString(_T("NONCONTINUABLE_EXCEPTION(%s)\r\n"), _T("�߳���ͼ��һ�����ɼ���ִ�е��쳣���������ִ��"));
			OutputString(_T("NONCONTINUABLE_EXCEPTION\r\n"));
		}
		return ;
	case EXCEPTION_STACK_OVERFLOW:
		{
			//OutputString(_T("STACK_OVERFLOW(%s)\r\n"), _T("ջ���"));
			OutputString(_T("STACK_OVERFLOW\r\n"));
		}
		return ;
	case EXCEPTION_INVALID_DISPOSITION:
		{
			//OutputString(_T("INVALID_DISPOSITION(%s)\r\n"), _T("�쳣���������쳣������������һ����Ч����")); //ʹ�ø߼����Ա�д�ĳ�����Զ������������쳣
			OutputString(_T("INVALID_DISPOSITION\r\n"));
		}
		return ;
	case EXCEPTION_FLT_UNDERFLOW:
		{
			//OutputString(_T("FLT_UNDERFLOW(%s)\r\n"), _T("������������ָ��С����Ӧ���͵���Сֵ"));
			OutputString(_T("FLT_UNDERFLOW\r\n"));
		}
		return ;
	case EXCEPTION_INT_OVERFLOW:
		{
			//OutputString(_T("INT_OVERFLOW(%s)\r\n"), _T("��������Խ��"));
			OutputString(_T("INT_OVERFLOW\r\n"));
		}
		return ;
	}

	TCHAR szBuffer[512] = { 0 };

	FormatMessage(  FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_HMODULE,
		GetModuleHandle( _T("NTDLL.DLL") ),
		dwExceptionCode, 0, szBuffer, sizeof( szBuffer ), 0 );

	OutputString(_T("%s"), szBuffer);
	OutputString(_T("\r\n"));
#endif
}

// Create minidump file for debugging
void CBaseException::create_minidump(PEXCEPTION_POINTERS pExceptionInfo)
{
#ifdef _WIN32
	if (g_log_folder.empty())
		return;

	std::time_t t = std::time(0);
	std::tm* now_time = std::localtime(&t);
	std::stringstream buf;
	buf << std::put_time(now_time, "crash_%a_%b_%d_%H_%M_%S_") << g_crash_log_count << ".dmp";
	
	auto log_folder = (boost::filesystem::path(g_log_folder) / "log").make_preferred();
	if (!boost::filesystem::exists(log_folder)) {
		boost::filesystem::create_directory(log_folder);
	}
	
	auto dump_path = boost::filesystem::path(log_folder / buf.str()).make_preferred();
	std::wstring dump_filename = dump_path.wstring();

	HANDLE hFile = CreateFileW(
		dump_filename.c_str(),
		GENERIC_WRITE,
		0,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (hFile != INVALID_HANDLE_VALUE) {
		MINIDUMP_EXCEPTION_INFORMATION mdei;
		mdei.ThreadId = GetCurrentThreadId();
		mdei.ExceptionPointers = pExceptionInfo;
		mdei.ClientPointers = FALSE;

		MINIDUMP_TYPE dumpType = (MINIDUMP_TYPE)(
			MiniDumpWithFullMemory |
			MiniDumpWithHandleData |
			MiniDumpWithThreadInfo |
			MiniDumpWithUnloadedModules
		);

		BOOL success = MiniDumpWriteDump(
			GetCurrentProcess(),
			GetCurrentProcessId(),
			hFile,
			dumpType,
			pExceptionInfo ? &mdei : NULL,
			NULL,
			NULL
		);

		CloseHandle(hFile);

		if (success) {
			BOOST_LOG_TRIVIAL(info) << "Minidump created: " << dump_path.string();
		} else {
			BOOST_LOG_TRIVIAL(error) << "Failed to create minidump. Error: " << GetLastError();
		}
	}
#endif
}

LONG WINAPI CBaseException::UnhandledExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo )
{
	if (pExceptionInfo->ExceptionRecord->ExceptionCode < 0x80000000
		//BBS: Load project on computers with SDC may trigger this exception (in ShowModal()),
		//     It's not fatal and should be ignored, or there will be lots of meaningless crash logs
		|| pExceptionInfo->ExceptionRecord->ExceptionCode==0xe0434352)
		//BBS: ignore the exception when copy preset
		//|| pExceptionInfo->ExceptionRecord->ExceptionCode==0xe06d7363)
	{
		//BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": got an ExceptionCode %1%, skip it!") % pExceptionInfo->ExceptionRecord->ExceptionCode;
		return EXCEPTION_CONTINUE_SEARCH;
	}
    g_dump_mutex.lock();
	
	// Create minidump first
	//create_minidump(pExceptionInfo);
	
	// Then create text log
	CBaseException base(GetCurrentProcess(), GetCurrentProcessId(), NULL, pExceptionInfo);
	base.ShowExceptionInformation();
    g_dump_mutex.unlock();

	return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI CBaseException::UnhandledExceptionFilter2(PEXCEPTION_POINTERS pExceptionInfo )
{
	// Create minidump first
	//create_minidump(pExceptionInfo);
	
	// Then create text log
	CBaseException base(GetCurrentProcess(), GetCurrentProcessId(), NULL, pExceptionInfo);
	base.ShowExceptionInformation();

	return EXCEPTION_CONTINUE_SEARCH;
}

BOOL CBaseException::GetLogicalAddress(
	PVOID addr, PTSTR szModule, DWORD len, DWORD& section, DWORD& offset )
{
	MEMORY_BASIC_INFORMATION mbi;

	if ( !VirtualQuery( addr, &mbi, sizeof(mbi) ) )
		return FALSE;

	DWORD_PTR hMod = (DWORD_PTR)mbi.AllocationBase;

	if ( !GetModuleFileName( (HMODULE)hMod, szModule, len ) )
		return FALSE;

	if (!hMod)
		return FALSE;

	PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)hMod;
	PIMAGE_NT_HEADERS pNtHdr = (PIMAGE_NT_HEADERS)(hMod + pDosHdr->e_lfanew);
	PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION( pNtHdr );

	DWORD_PTR rva = (DWORD_PTR)addr - hMod;

	for (unsigned i = 0; i < pNtHdr->FileHeader.NumberOfSections; i++, pSection++ )
	{
		DWORD sectionStart = pSection->VirtualAddress;
		DWORD sectionEnd = sectionStart + max(pSection->SizeOfRawData, pSection->Misc.VirtualSize);

		if ( (rva >= sectionStart) && (rva <= sectionEnd) )
		{
			section = i+1;
			offset = rva - sectionStart;
			return TRUE;
		}
	}

	return FALSE;   // Should never get here!
}

void CBaseException::ShowRegistorInformation(PCONTEXT pCtx)
{
#if defined(_M_IX86) // Intel Only!
	OutputString( _T("\nRegisters:\r\n") );

	OutputString(_T("EAX:%08X\r\nEBX:%08X\r\nECX:%08X\r\nEDX:%08X\r\nESI:%08X\r\nEDI:%08X\r\n"),
		pCtx->Eax, pCtx->Ebx, pCtx->Ecx, pCtx->Edx, pCtx->Esi, pCtx->Edi );

	OutputString( _T("CS:EIP:%04X:%08X\r\n"), pCtx->SegCs, pCtx->Eip );
	OutputString( _T("SS:ESP:%04X:%08X  EBP:%08X\r\n"),pCtx->SegSs, pCtx->Esp, pCtx->Ebp );
	OutputString( _T("DS:%04X  ES:%04X  FS:%04X  GS:%04X\r\n"), pCtx->SegDs, pCtx->SegEs, pCtx->SegFs, pCtx->SegGs );
	OutputString( _T("Flags:%08X\r\n"), pCtx->EFlags );
#elif defined(_M_X64)
    OutputString(_T("\nRegisters:\r\n"));

	OutputString(_T("RAX:%016llX\r\nRBX:%016llX\r\nRCX:%016llX\r\nRDX:%016llX\r\nRSI:%016llX\r\nRDI:%016llX\r\n"),
		pCtx->Rax, pCtx->Rbx, pCtx->Rcx, pCtx->Rdx, pCtx->Rsi, pCtx->Rdi );

	OutputString(_T("R8:%016llX\r\nR9:%016llX\r\nR10:%016llX\r\nR11:%016llX\r\nR12:%016llX\r\nR13:%016llX\r\nR14:%016llX\r\nR15:%016llX\r\n"),
		pCtx->R8, pCtx->R9, pCtx->R10, pCtx->R11, pCtx->R12, pCtx->R13, pCtx->R14, pCtx->R15 );

    OutputString(_T("CS:RIP:%04X:%016llX\r\n"), pCtx->SegCs, pCtx->Rip);
    OutputString(_T("SS:RSP:%04X:%016llX  RBP:%016llX\r\n"), pCtx->SegSs, pCtx->Rsp, pCtx->Rbp);
    OutputString(_T("DS:%04X  ES:%04X  FS:%04X  GS:%04X\r\n"), pCtx->SegDs, pCtx->SegEs, pCtx->SegFs, pCtx->SegGs);
    OutputString(_T("Flags:%08X\r\n"), pCtx->EFlags);
#endif

	OutputString( _T("\r\n") );
}

void CBaseException::STF(unsigned int ui,  PEXCEPTION_POINTERS pEp)
{
	CBaseException base(GetCurrentProcess(), GetCurrentProcessId(), NULL, pEp);
	throw base;
}

void CBaseException::ShowExceptionInformation()
{
	// Crash report header
	OutputString(_T("================================================================================\r\n"));
	OutputString(_T("                        ELEGOOSLICER CRASH REPORT                              \r\n"));
	OutputString(_T("================================================================================\r\n\r\n"));

	// Timestamp
	std::time_t t = std::time(0);
	std::tm* now_time = std::localtime(&t);
	TCHAR time_buffer[128];
	_tcsftime(time_buffer, sizeof(time_buffer)/sizeof(TCHAR), _T("%Y-%m-%d %H:%M:%S"), now_time);
	OutputString(_T("Crash Time: %s\r\n"), time_buffer);

	// Version information
	OutputString(_T("ElegooSlicer Version: %s\r\n"), _T(ELEGOOSLICER_VERSION));
	OutputString(_T("\r\n"));

	// System information
	OutputString(_T("---- SYSTEM INFORMATION ----\r\n"));
	
	// Computer name
	TCHAR computerName[MAX_COMPUTERNAME_LENGTH + 1];
	DWORD compNameSize = sizeof(computerName) / sizeof(TCHAR);
	if (GetComputerName(computerName, &compNameSize)) {
		OutputString(_T("Computer Name: %s\r\n"), computerName);
	}
	
	// OS version
	OSVERSIONINFOEX osvi = {};
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
#pragma warning(push)
#pragma warning(disable: 4996)
	if (GetVersionEx((OSVERSIONINFO*)&osvi)) {
		OutputString(_T("OS Version: %d.%d Build %d %s\r\n"), 
			osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber, osvi.szCSDVersion);
		OutputString(_T("OS Type: %s\r\n"), (osvi.wProductType == VER_NT_WORKSTATION) ? _T("Workstation") : _T("Server"));
	}
#pragma warning(pop)

	// CPU information
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	OutputString(_T("Number of Processors: %d\r\n"), sysInfo.dwNumberOfProcessors);
	OutputString(_T("Processor Architecture: "));
	switch (sysInfo.wProcessorArchitecture) {
		case PROCESSOR_ARCHITECTURE_AMD64:
			OutputString(_T("x64 (AMD or Intel)\r\n"));
			break;
		case PROCESSOR_ARCHITECTURE_INTEL:
			OutputString(_T("x86\r\n"));
			break;
		case PROCESSOR_ARCHITECTURE_ARM:
			OutputString(_T("ARM\r\n"));
			break;
		case PROCESSOR_ARCHITECTURE_ARM64:
			OutputString(_T("ARM64\r\n"));
			break;
		default:
			OutputString(_T("Unknown (%d)\r\n"), sysInfo.wProcessorArchitecture);
			break;
	}

	// System memory info
	MEMORYSTATUSEX memInfo = {};
	memInfo.dwLength = sizeof(MEMORYSTATUSEX);
	if (GlobalMemoryStatusEx(&memInfo)) {
		OutputString(_T("Total Physical Memory: %llu MB\r\n"), memInfo.ullTotalPhys / (1024 * 1024));
		OutputString(_T("Available Physical Memory: %llu MB\r\n"), memInfo.ullAvailPhys / (1024 * 1024));
		OutputString(_T("Memory Load: %d%%\r\n"), memInfo.dwMemoryLoad);
	}

	// Process information
	OutputString(_T("Process ID: %d\r\n"), GetCurrentProcessId());
	OutputString(_T("Thread ID: %d\r\n"), GetCurrentThreadId());
	
	// Get process memory usage
	PROCESS_MEMORY_COUNTERS_EX pmc = {};
	pmc.cb = sizeof(pmc);
	if (GetProcessMemoryInfo(GetCurrentProcess(), (void*)&pmc, sizeof(pmc))) {
		OutputString(_T("Process Working Set: %llu MB\r\n"), pmc.WorkingSetSize / (1024 * 1024));
		OutputString(_T("Process Peak Working Set: %llu MB\r\n"), pmc.PeakWorkingSetSize / (1024 * 1024));
		OutputString(_T("Process Private Bytes: %llu MB\r\n"), pmc.PrivateUsage / (1024 * 1024));
	}

	OutputString(_T("\r\n"));

	// Exception information
	OutputString(_T("---- EXCEPTION INFORMATION ----\r\n"));
	ShowExceptionResoult(m_pEp->ExceptionRecord->ExceptionCode);

	OutputString(_T("Exception Flags: 0x%x "), m_pEp->ExceptionRecord->ExceptionFlags);
	if (m_pEp->ExceptionRecord->ExceptionFlags == EXCEPTION_NONCONTINUABLE) {
		OutputString(_T("(NONCONTINUABLE)\r\n"));
	} else {
		OutputString(_T("(CONTINUABLE)\r\n"));
	}
	
	OutputString(_T("Exception Record Address: 0x%p\r\n"), m_pEp->ExceptionRecord);
	OutputString(_T("Number of Parameters: %ld\r\n"), m_pEp->ExceptionRecord->NumberParameters);
	
	for (int i = 0; i < m_pEp->ExceptionRecord->NumberParameters; i++)
	{
		OutputString(_T("  Parameter[%d]: 0x%p\r\n"), i, (void*)m_pEp->ExceptionRecord->ExceptionInformation[i]);
	}

	// Context information
	OutputString(_T("Context Record: 0x%p\r\n"), m_pEp->ContextRecord);
	OutputString(_T("Context Flags: 0x%x, EFlags: 0x%x\r\n"), 
		m_pEp->ContextRecord->ContextFlags, m_pEp->ContextRecord->EFlags);

	// Fault address with module info
	TCHAR szFaultingModule[MAX_PATH];
	DWORD section, offset;
	GetLogicalAddress(m_pEp->ExceptionRecord->ExceptionAddress, szFaultingModule, sizeof(szFaultingModule), section, offset);
	OutputString(_T("\r\nFault Address: 0x%p\r\n"), m_pEp->ExceptionRecord->ExceptionAddress);
	OutputString(_T("Faulting Module: %s\r\n"), szFaultingModule);
	OutputString(_T("Module Section:Offset: 0x%X:0x%X\r\n"), section, offset);

	OutputString(_T("\r\n"));

	// Register dump
	ShowRegistorInformation(m_pEp->ContextRecord);

	// Call stack
	OutputString(_T("---- CALL STACK ----\r\n"));
	ShowCallstack(GetCurrentThread(), m_pEp->ContextRecord);

	OutputString(_T("\r\n"));

	// Loaded modules
	OutputString(_T("---- LOADED MODULES ----\r\n"));
	ShowLoadModules();

	OutputString(_T("\r\n"));
	OutputString(_T("================================================================================\r\n"));
	OutputString(_T("                           END OF CRASH REPORT                                 \r\n"));
	OutputString(_T("================================================================================\r\n"));
}