
#include <windows.h>
#include <iostream>
#include <vector>
#include <string>

using namespace std;

const int LIMIT = 10000;
const int PROCESS_COUNT = 10;
const int RANGE_SIZE = 1000;

bool isPrime(int number) {
    if (number < 2) return false;

    for (int i = 2; i * i <= number; i++) {
        if (number % i == 0) return false;
    }

    return true;
}

bool writeAll(HANDLE pipe, const void* data, DWORD size) {
    const char* ptr = static_cast<const char*>(data);

    while (size > 0) {
        DWORD written = 0;

        if (!WriteFile(pipe, ptr, size, &written, NULL) || written == 0)
            return false;

        ptr += written;
        size -= written;
    }

    return true;
}

bool readAll(HANDLE pipe, void* data, DWORD size) {
    char* ptr = static_cast<char*>(data);

    while (size > 0) {
        DWORD received = 0;

        if (!ReadFile(pipe, ptr, size, &received, NULL) || received == 0)
            return false;

        ptr += received;
        size -= received;
    }

    return true;
}

int childMode() {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);

    int start, end;

    if (!readAll(input, &start, sizeof(start)) ||
        !readAll(input, &end, sizeof(end))) {
        return 1;
    }

    vector<int> primes;

    for (int number = start; number <= end; number++) {
        if (isPrime(number))
            primes.push_back(number);
    }

    int count = static_cast<int>(primes.size());

    if (!writeAll(output, &count, sizeof(count)))
        return 1;

    if (count > 0 &&
        !writeAll(output, primes.data(),
                  static_cast<DWORD>(primes.size() * sizeof(int)))) {
        return 1;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc > 1 && string(argv[1]) == "--child")
        return childMode();

    char executablePath[MAX_PATH];

    DWORD pathLength = GetModuleFileNameA(
        NULL, executablePath, MAX_PATH
    );

    if (pathLength == 0 || pathLength >= MAX_PATH) {
        cerr << "Nu s-a putut obtine calea executabilului.\n";
        return 1;
    }

    int totalPrimes = 0;

    cout << "Prime numbers up to " << LIMIT << ":\n";

    for (int i = 0; i < PROCESS_COUNT; i++) {
        HANDLE childInputRead, parentInputWrite;
        HANDLE parentOutputRead, childOutputWrite;

        SECURITY_ATTRIBUTES security;
        security.nLength = sizeof(SECURITY_ATTRIBUTES);
        security.lpSecurityDescriptor = NULL;
        security.bInheritHandle = TRUE;

        if (!CreatePipe(&childInputRead, &parentInputWrite,
                        &security, 0)) {
            cerr << "Eroare la crearea pipe-ului de intrare.\n";
            return 1;
        }

        if (!CreatePipe(&parentOutputRead, &childOutputWrite,
                        &security, 0)) {
            cerr << "Eroare la crearea pipe-ului de iesire.\n";
            return 1;
        }

        SetHandleInformation(parentInputWrite, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(parentOutputRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA startupInfo = {};
        startupInfo.cb = sizeof(STARTUPINFOA);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdInput = childInputRead;
        startupInfo.hStdOutput = childOutputWrite;
        startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

        PROCESS_INFORMATION processInfo = {};

        string commandLine = "\"" + string(executablePath) + "\" --child";
        vector<char> commandBuffer(commandLine.begin(), commandLine.end());
        commandBuffer.push_back('\0');

        BOOL created = CreateProcessA(
            NULL,
            commandBuffer.data(),
            NULL,
            NULL,
            TRUE,
            0,
            NULL,
            NULL,
            &startupInfo,
            &processInfo
        );

        CloseHandle(childInputRead);
        CloseHandle(childOutputWrite);

        if (!created) {
            cerr << "Nu s-a putut crea procesul copil " << i + 1 << ".\n";
            CloseHandle(parentInputWrite);
            CloseHandle(parentOutputRead);
            return 1;
        }

        int start = i * RANGE_SIZE + 1;
        int end = (i + 1) * RANGE_SIZE;

        bool sent = writeAll(parentInputWrite, &start, sizeof(start)) &&
                    writeAll(parentInputWrite, &end, sizeof(end));

        CloseHandle(parentInputWrite);

        int count = 0;
        bool received = sent &&
                        readAll(parentOutputRead, &count, sizeof(count));

        vector<int> primes;

        if (received && count > 0) {
            primes.resize(count);
            received = readAll(
                parentOutputRead,
                primes.data(),
                static_cast<DWORD>(primes.size() * sizeof(int))
            );
        }

        CloseHandle(parentOutputRead);

        WaitForSingleObject(processInfo.hProcess, INFINITE);

        DWORD exitCode = 1;
        GetExitCodeProcess(processInfo.hProcess, &exitCode);

        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);

        if (!received || exitCode != 0) {
            cerr << "Eroare la comunicarea cu procesul copil "
                 << i + 1 << ".\n";
            return 1;
        }

        cout << "Process " << i + 1 << " (" << start
             << "-" << end << "): ";

        for (int prime : primes)
            cout << prime << " ";

        cout << "\n";
        totalPrimes += count;
    }

    cout << "\nTotal prime numbers found: " << totalPrimes << "\n";

    return 0;
}
