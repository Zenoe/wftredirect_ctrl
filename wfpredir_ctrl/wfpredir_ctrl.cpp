/*
 * wfpredir_ctrl.c  —  User-mode control tool
 *
 * Usage:
 *   wfpredir_ctrl set <PID> <dest_ip>     start redirecting PID to dest_ip
 *   wfpredir_ctrl clear                   stop redirecting
 *
 * Example:
 *   wfpredir_ctrl set 1234 10.8.0.2
 *
 * Compile (x64 Native Tools Command Prompt for VS 2022):
 *   cl wfpredir_ctrl.c /Fe:wfpredir_ctrl.exe
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

 //
 // Must match definitions in wfpredirect.h exactly
 //
#define WFPREDIR_IOCTL_BASE     0x8000

#define IOCTL_WFPREDIR_SET_PID \
    CTL_CODE(FILE_DEVICE_UNKNOWN, WFPREDIR_IOCTL_BASE + 1, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_WFPREDIR_SET_DEST_IP \
    CTL_CODE(FILE_DEVICE_UNKNOWN, WFPREDIR_IOCTL_BASE + 2, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_WFPREDIR_CLEAR \
    CTL_CODE(FILE_DEVICE_UNKNOWN, WFPREDIR_IOCTL_BASE + 3, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Parse "a.b.c.d" → ULONG host byte order
static BOOL ParseIpv4(const char* str, ULONG* out)
{
    unsigned int a, b, c, d;
    if (sscanf_s(str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return FALSE;
    if (a > 255 || b > 255 || c > 255 || d > 255) return FALSE;
    *out = (a << 24) | (b << 16) | (c << 8) | d;
    return TRUE;
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage:\n");
        printf("  wfpredir_ctrl set <PID> <dest_ip>\n");
        printf("  wfpredir_ctrl clear\n");
        return 1;
    }

    // Open the driver device
    HANDLE hDevice = CreateFileA(
        "\\\\.\\WfpRedirect",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("[!] Cannot open \\\\.\\WfpRedirect  (error %lu)\n"
            "    Is the driver loaded?  Run: sc start WfpRedirect\n",
            GetLastError());
        return 1;
    }

    DWORD bytesReturned = 0;
    BOOL  ok = FALSE;

    if (_stricmp(argv[1], "set") == 0)
    {
        if (argc < 4) {
            printf("Usage: wfpredir_ctrl set <PID> <dest_ip>\n");
            CloseHandle(hDevice);
            return 1;
        }

        ULONG pid = (ULONG)atol(argv[2]);
        ULONG ip = 0;

        if (!ParseIpv4(argv[3], &ip)) {
            printf("[!] Invalid IP address: %s\n", argv[3]);
            CloseHandle(hDevice);
            return 1;
        }

        // Send dest IP first
        ok = DeviceIoControl(hDevice, IOCTL_WFPREDIR_SET_DEST_IP,
            &ip, sizeof(ip), NULL, 0, &bytesReturned, NULL);
        if (!ok) {
            printf("[!] IOCTL_WFPREDIR_SET_DEST_IP failed: %lu\n", GetLastError());
            CloseHandle(hDevice);
            return 1;
        }
        printf("[+] Dest IP set to %s\n", argv[3]);

        // Then activate by setting PID
        ok = DeviceIoControl(hDevice, IOCTL_WFPREDIR_SET_PID,
            &pid, sizeof(pid), NULL, 0, &bytesReturned, NULL);
        if (!ok) {
            printf("[!] IOCTL_WFPREDIR_SET_PID failed: %lu\n", GetLastError());
            CloseHandle(hDevice);
            return 1;
        }
        printf("[+] Redirecting PID %lu  -->  %s\n", pid, argv[3]);
        printf("    Watch DebugView for per-connection logs.\n");
    }
    else if (_stricmp(argv[1], "clear") == 0)
    {
        ok = DeviceIoControl(hDevice, IOCTL_WFPREDIR_CLEAR,
            NULL, 0, NULL, 0, &bytesReturned, NULL);
        if (!ok) {
            printf("[!] IOCTL_WFPREDIR_CLEAR failed: %lu\n", GetLastError());
            CloseHandle(hDevice);
            return 1;
        }
        printf("[+] Redirection cleared.\n");
    }
    else
    {
        printf("[!] Unknown command: %s\n", argv[1]);
        CloseHandle(hDevice);
        return 1;
    }

    CloseHandle(hDevice);
    return 0;
}