#include "../header/Injector.h"
#include <TlHelp32.h>
#include <cstdio>

namespace Injector {
    static LogCallback g_logCallback = nullptr;

    void SetLogCallback(LogCallback callback) {
        g_logCallback = callback;
    }

    static void Log(const char* msg, float r = 1.0f, float g = 1.0f, float b = 1.0f) {
        if (g_logCallback) {
            g_logCallback(msg, r, g, b);
        }
    }

    // Loader structures
    struct LoaderData {
        LPVOID ImageBase;
        PIMAGE_NT_HEADERS NtHeaders;
        PIMAGE_BASE_RELOCATION BaseReloc;
        PIMAGE_IMPORT_DESCRIPTOR ImportDir;
        HMODULE(WINAPI* fnLoadLibraryA)(LPCSTR);
        FARPROC(WINAPI* fnGetProcAddress)(HMODULE, LPCSTR);
        BOOL(WINAPI* fnDllMain)(HMODULE, DWORD, LPVOID);
    };

    struct EncryptedLoaderData {
        LPVOID ImageBase;
        PIMAGE_NT_HEADERS NtHeaders;
        PIMAGE_BASE_RELOCATION BaseReloc;
        PIMAGE_IMPORT_DESCRIPTOR ImportDir;
        BYTE XorKey;
        DWORD RelocRVA;
        DWORD SectionCount;
        struct { DWORD VA, Size, Chars; } Sections[96];
        HMODULE(WINAPI* fnLoadLibraryA)(LPCSTR);
        FARPROC(WINAPI* fnGetProcAddress)(HMODULE, LPCSTR);
        BOOL(WINAPI* fnDllMain)(HMODULE, DWORD, LPVOID);
    };

    // Loader stubs
    DWORD WINAPI LoaderStub(LoaderData* d) {
        if (!d) return 0;
        BYTE* base = (BYTE*)d->ImageBase;

        if (d->BaseReloc && d->NtHeaders) {
            DWORD_PTR delta = (DWORD_PTR)base - d->NtHeaders->OptionalHeader.ImageBase;
            if (delta) {
                auto reloc = d->BaseReloc;
                while (reloc->VirtualAddress) {
                    WORD* relocData = (WORD*)((BYTE*)reloc + sizeof(IMAGE_BASE_RELOCATION));
                    DWORD count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
                    for (DWORD i = 0; i < count; i++) {
                        if ((relocData[i] >> 12) == IMAGE_REL_BASED_DIR64) {
                            *(DWORD_PTR*)(base + reloc->VirtualAddress + (relocData[i] & 0xFFF)) += delta;
                        }
                    }
                    reloc = (PIMAGE_BASE_RELOCATION)((BYTE*)reloc + reloc->SizeOfBlock);
                }
            }
        }

        if (d->ImportDir) {
            auto imp = d->ImportDir;
            while (imp->Name) {
                HMODULE hMod = d->fnLoadLibraryA((char*)(base + imp->Name));
                if (hMod) {
                    auto thunk = (PIMAGE_THUNK_DATA)(base + imp->FirstThunk);
                    while (thunk->u1.AddressOfData) {
                        if (thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) {
                            thunk->u1.Function = (DWORD_PTR)d->fnGetProcAddress(hMod, (LPCSTR)(thunk->u1.Ordinal & 0xFFFF));
                        }
                        else {
                            auto import = (PIMAGE_IMPORT_BY_NAME)(base + thunk->u1.AddressOfData);
                            thunk->u1.Function = (DWORD_PTR)d->fnGetProcAddress(hMod, import->Name);
                        }
                        thunk++;
                    }
                }
                imp++;
            }
        }

        return d->fnDllMain ? d->fnDllMain((HMODULE)base, DLL_PROCESS_ATTACH, NULL) : 1;
    }

    DWORD WINAPI EncryptedLoaderStub(EncryptedLoaderData* d) {
        if (!d) return 0;
        BYTE* base = (BYTE*)d->ImageBase;

        for (DWORD i = 0; i < d->SectionCount; i++) {
            if (d->Sections[i].Chars & IMAGE_SCN_MEM_EXECUTE) {
                bool isReloc = (d->RelocRVA >= d->Sections[i].VA &&
                    d->RelocRVA < d->Sections[i].VA + d->Sections[i].Size);
                if (!isReloc && d->Sections[i].Size) {
                    for (DWORD j = 0; j < d->Sections[i].Size; j++) {
                        base[d->Sections[i].VA + j] ^= d->XorKey;
                    }
                }
            }
        }

        if (d->BaseReloc && d->NtHeaders) {
            DWORD_PTR delta = (DWORD_PTR)base - d->NtHeaders->OptionalHeader.ImageBase;
            if (delta) {
                auto reloc = d->BaseReloc;
                while (reloc->VirtualAddress) {
                    WORD* relocData = (WORD*)((BYTE*)reloc + sizeof(IMAGE_BASE_RELOCATION));
                    DWORD count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
                    for (DWORD i = 0; i < count; i++) {
                        if ((relocData[i] >> 12) == IMAGE_REL_BASED_DIR64) {
                            *(DWORD_PTR*)(base + reloc->VirtualAddress + (relocData[i] & 0xFFF)) += delta;
                        }
                    }
                    reloc = (PIMAGE_BASE_RELOCATION)((BYTE*)reloc + reloc->SizeOfBlock);
                }
            }
        }

        if (d->ImportDir) {
            auto imp = d->ImportDir;
            while (imp->Name) {
                HMODULE hMod = d->fnLoadLibraryA((char*)(base + imp->Name));
                if (hMod) {
                    auto thunk = (PIMAGE_THUNK_DATA)(base + imp->FirstThunk);
                    while (thunk->u1.AddressOfData) {
                        if (thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) {
                            thunk->u1.Function = (DWORD_PTR)d->fnGetProcAddress(hMod, (LPCSTR)(thunk->u1.Ordinal & 0xFFFF));
                        }
                        else {
                            auto import = (PIMAGE_IMPORT_BY_NAME)(base + thunk->u1.AddressOfData);
                            thunk->u1.Function = (DWORD_PTR)d->fnGetProcAddress(hMod, import->Name);
                        }
                        thunk++;
                    }
                }
                imp++;
            }
        }

        return d->fnDllMain ? d->fnDllMain((HMODULE)base, DLL_PROCESS_ATTACH, NULL) : 1;
    }

    DWORD FindProcess(const char* name) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;

        PROCESSENTRY32W pe = { sizeof(pe) };
        DWORD pid = 0;

        if (Process32FirstW(snap, &pe)) {
            do {
                char buf[MAX_PATH];
                WideCharToMultiByte(CP_ACP, 0, pe.szExeFile, -1, buf, MAX_PATH, 0, 0);
                if (_stricmp(buf, name) == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }

        CloseHandle(snap);
        return pid;
    }

    bool StandardInject(const char* dll, DWORD pid) {
        HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, 0, pid);
        if (!proc) {
            Log("Failed to open process", 1.0f, 0.3f, 0.3f);
            return false;
        }

        size_t len = strlen(dll) + 1;
        void* mem = VirtualAllocEx(proc, 0, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        if (!mem || !WriteProcessMemory(proc, mem, dll, len, 0)) {
            Log("Memory operation failed", 1.0f, 0.3f, 0.3f);
            if (mem) VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
            CloseHandle(proc);
            return false;
        }

        auto loadLib = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
        HANDLE thread = CreateRemoteThread(proc, 0, 0, (LPTHREAD_START_ROUTINE)loadLib, mem, 0, 0);

        if (!thread) {
            Log("Thread creation failed", 1.0f, 0.3f, 0.3f);
            VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
            CloseHandle(proc);
            return false;
        }

        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
        CloseHandle(proc);

        Log("Injection successful", 0.3f, 1.0f, 0.3f);
        return true;
    }

    bool ManualMapInject(const char* dll, DWORD pid, bool encrypt) {
        HANDLE file = CreateFileA(dll, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
        if (file == INVALID_HANDLE_VALUE) {
            Log("Failed to open DLL file", 1.0f, 0.3f, 0.3f);
            return false;
        }

        DWORD size = GetFileSize(file, 0);
        BYTE* data = (BYTE*)VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        if (!data || !ReadFile(file, data, size, &size, 0)) {
            Log("Failed to read DLL", 1.0f, 0.3f, 0.3f);
            if (data) VirtualFree(data, 0, MEM_RELEASE);
            CloseHandle(file);
            return false;
        }
        CloseHandle(file);

        auto dos = (PIMAGE_DOS_HEADER)data;
        auto nt = (PIMAGE_NT_HEADERS)(data + dos->e_lfanew);

        if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE) {
            Log("Invalid PE format", 1.0f, 0.3f, 0.3f);
            VirtualFree(data, 0, MEM_RELEASE);
            return false;
        }

        BYTE key = 0;
        if (encrypt) {
            key = (BYTE)(rand() % 255 + 1);
            auto sec = IMAGE_FIRST_SECTION(nt);
            DWORD relocRVA = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
            int count = 0;

            for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
                if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
                    bool isReloc = relocRVA >= sec[i].VirtualAddress &&
                        relocRVA < sec[i].VirtualAddress + sec[i].Misc.VirtualSize;
                    if (!isReloc && sec[i].SizeOfRawData) {
                        for (DWORD j = 0; j < sec[i].SizeOfRawData; j++) {
                            data[sec[i].PointerToRawData + j] ^= key;
                        }
                        count++;
                    }
                }
            }

            char buf[128];
            sprintf_s(buf, "Encrypted %d sections (key: 0x%02X)", count, key);
            Log(buf, 0.5f, 0.8f, 1.0f);
        }

        HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, 0, pid);
        if (!proc) {
            Log("Failed to open process", 1.0f, 0.3f, 0.3f);
            VirtualFree(data, 0, MEM_RELEASE);
            return false;
        }

        void* remote = VirtualAllocEx(proc, 0, nt->OptionalHeader.SizeOfImage,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

        if (!remote) {
            Log("Failed to allocate in target", 1.0f, 0.3f, 0.3f);
            CloseHandle(proc);
            VirtualFree(data, 0, MEM_RELEASE);
            return false;
        }

        WriteProcessMemory(proc, remote, data, nt->OptionalHeader.SizeOfHeaders, 0);
        auto sec = IMAGE_FIRST_SECTION(nt);

        for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
            if (sec[i].SizeOfRawData) {
                WriteProcessMemory(proc, (BYTE*)remote + sec[i].VirtualAddress,
                    data + sec[i].PointerToRawData, sec[i].SizeOfRawData, 0);
            }
        }

        auto k32 = GetModuleHandleA("kernel32.dll");
        void* loaderData = 0;
        void* loaderFunc = 0;

        if (encrypt) {
            EncryptedLoaderData ld = {};
            ld.ImageBase = remote;
            ld.NtHeaders = (PIMAGE_NT_HEADERS)((BYTE*)remote + dos->e_lfanew);
            ld.XorKey = key;
            ld.RelocRVA = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
            ld.SectionCount = nt->FileHeader.NumberOfSections;

            for (int i = 0; i < nt->FileHeader.NumberOfSections && i < 96; i++) {
                ld.Sections[i].VA = sec[i].VirtualAddress;
                ld.Sections[i].Size = sec[i].Misc.VirtualSize;
                ld.Sections[i].Chars = sec[i].Characteristics;
            }

            if (nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size)
                ld.BaseReloc = (PIMAGE_BASE_RELOCATION)((BYTE*)remote + ld.RelocRVA);
            if (nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size)
                ld.ImportDir = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)remote +
                    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

            ld.fnLoadLibraryA = (decltype(ld.fnLoadLibraryA))GetProcAddress(k32, "LoadLibraryA");
            ld.fnGetProcAddress = (decltype(ld.fnGetProcAddress))GetProcAddress(k32, "GetProcAddress");
            ld.fnDllMain = (decltype(ld.fnDllMain))((BYTE*)remote + nt->OptionalHeader.AddressOfEntryPoint);

            loaderData = VirtualAllocEx(proc, 0, sizeof(ld), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            WriteProcessMemory(proc, loaderData, &ld, sizeof(ld), 0);
            loaderFunc = VirtualAllocEx(proc, 0, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            WriteProcessMemory(proc, loaderFunc, (void*)EncryptedLoaderStub, 4096, 0);
        }
        else {
            LoaderData ld = {};
            ld.ImageBase = remote;
            ld.NtHeaders = (PIMAGE_NT_HEADERS)((BYTE*)remote + dos->e_lfanew);

            if (nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size)
                ld.BaseReloc = (PIMAGE_BASE_RELOCATION)((BYTE*)remote +
                    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
            if (nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size)
                ld.ImportDir = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)remote +
                    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

            ld.fnLoadLibraryA = (decltype(ld.fnLoadLibraryA))GetProcAddress(k32, "LoadLibraryA");
            ld.fnGetProcAddress = (decltype(ld.fnGetProcAddress))GetProcAddress(k32, "GetProcAddress");
            ld.fnDllMain = (decltype(ld.fnDllMain))((BYTE*)remote + nt->OptionalHeader.AddressOfEntryPoint);

            loaderData = VirtualAllocEx(proc, 0, sizeof(ld), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            WriteProcessMemory(proc, loaderData, &ld, sizeof(ld), 0);
            loaderFunc = VirtualAllocEx(proc, 0, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            WriteProcessMemory(proc, loaderFunc, (void*)LoaderStub, 4096, 0);
        }

        HANDLE thread = CreateRemoteThread(proc, 0, 0, (LPTHREAD_START_ROUTINE)loaderFunc, loaderData, 0, 0);
        if (!thread) {
            Log("Thread creation failed", 1.0f, 0.3f, 0.3f);
            VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
            VirtualFreeEx(proc, loaderData, 0, MEM_RELEASE);
            VirtualFreeEx(proc, loaderFunc, 0, MEM_RELEASE);
            CloseHandle(proc);
            VirtualFree(data, 0, MEM_RELEASE);
            return false;
        }

        WaitForSingleObject(thread, INFINITE);
        DWORD code = 0;
        GetExitCodeThread(thread, &code);

        CloseHandle(thread);
        VirtualFreeEx(proc, loaderData, 0, MEM_RELEASE);
        VirtualFreeEx(proc, loaderFunc, 0, MEM_RELEASE);
        CloseHandle(proc);
        VirtualFree(data, 0, MEM_RELEASE);

        if (code) {
            Log(encrypt ? "Encrypted manual map complete" : "Manual map complete", 0.3f, 1.0f, 0.3f);
            return true;
        }

        Log("Injection failed (DllMain returned 0)", 1.0f, 0.3f, 0.3f);
        return false;
    }
}