// Cautarea numerelor prime pana la 10000 folosind 10 procese si pipe-uri (Windows)
// Compilare (MinGW): g++ -Wall -o prime_windows.exe prime_windows.cpp
// Compilare (MSVC):  cl /EHsc prime_windows.cpp
// Rulare:            prime_windows.exe
//
// Pe Windows nu exista fork(), asa ca procesul initial isi porneste
// propriul executabil de 10 ori cu CreateProcess (argumentul "copil").
// Fiecare copil primeste intervalul pe stdin (pipe anonim) si trimite
// numerele prime pe stdout (alt pipe anonim) catre procesul initial.

#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

const int NUM_PROC = 10;   // numarul de procese copil
const int RANGE    = 1000; // dimensiunea intervalului pentru fiecare proces

bool isPrime(int n) {
    if (n < 2) return false;
    for (int d = 2; d * d <= n; ++d)
        if (n % d == 0) return false;
    return true;
}

// Scrie exact len octeti in pipe
bool writeAll(HANDLE h, const void* buf, DWORD len) {
    const char* p = static_cast<const char*>(buf);
    while (len > 0) {
        DWORD w = 0;
        if (!WriteFile(h, p, len, &w, NULL) || w == 0) return false;
        p += w; len -= w;
    }
    return true;
}

// Citeste exact len octeti din pipe
bool readAll(HANDLE h, void* buf, DWORD len) {
    char* p = static_cast<char*>(buf);
    while (len > 0) {
        DWORD r = 0;
        if (!ReadFile(h, p, len, &r, NULL) || r == 0) return false;
        p += r; len -= r;
    }
    return true;
}

// ===== PROCES COPIL =====
int runChild() {
    HANDLE hIn  = GetStdHandle(STD_INPUT_HANDLE);   // capatul de citire al pipe-ului parinte -> copil
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);  // capatul de scriere al pipe-ului copil -> parinte

    int interval[2];
    if (!readAll(hIn, interval, sizeof(interval))) return 1;

    std::vector<int> primes;
    for (int n = interval[0]; n <= interval[1]; ++n)
        if (isPrime(n)) primes.push_back(n);

    int count = static_cast<int>(primes.size());
    writeAll(hOut, &count, sizeof(count));
    if (count > 0)
        writeAll(hOut, primes.data(), count * sizeof(int));
    return 0;
}

// ===== PROCES PARINTE =====
int main(int argc, char* argv[]) {
    if (argc > 1 && std::strcmp(argv[1], "copil") == 0)
        return runChild();

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    HANDLE toChildWrite[NUM_PROC];   // parintele scrie intervalul aici
    HANDLE fromChildRead[NUM_PROC];  // parintele citeste numerele prime de aici
    PROCESS_INFORMATION pi[NUM_PROC];

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;        // pipe-urile pot fi mostenite de copii

    std::cout << "Procesul initial (PID " << GetCurrentProcessId() << ") creeaza "
              << NUM_PROC << " procese...\n\n";

    for (int i = 0; i < NUM_PROC; ++i) {
        HANDLE childStdinRead, childStdoutWrite;

        if (!CreatePipe(&childStdinRead, &toChildWrite[i], &sa, 0) ||
            !CreatePipe(&fromChildRead[i], &childStdoutWrite, &sa, 0)) {
            std::cerr << "CreatePipe a esuat: " << GetLastError() << "\n";
            return 1;
        }
        // capetele parintelui NU trebuie mostenite de copii
        SetHandleInformation(toChildWrite[i], HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(fromChildRead[i], HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput  = childStdinRead;
        si.hStdOutput = childStdoutWrite;
        si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

        std::string cmd = std::string("\"") + exePath + "\" copil";
        std::vector<char> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back('\0');

        ZeroMemory(&pi[i], sizeof(pi[i]));
        if (!CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, TRUE, 0,
                            NULL, NULL, &si, &pi[i])) {
            std::cerr << "CreateProcess a esuat: " << GetLastError() << "\n";
            return 1;
        }

        // parintele inchide capetele care apartin copilului
        CloseHandle(childStdinRead);
        CloseHandle(childStdoutWrite);

        // trimitem intervalul prin pipe
        int interval[2] = { i * RANGE + 1, (i + 1) * RANGE };
        writeAll(toChildWrite[i], interval, sizeof(interval));
        CloseHandle(toChildWrite[i]);
    }

    // Parintele primeste si afiseaza numerele prime de la fiecare proces
    int total = 0;
    for (int i = 0; i < NUM_PROC; ++i) {
        int count = 0;
        readAll(fromChildRead[i], &count, sizeof(count));
        std::vector<int> primes(count);
        if (count > 0)
            readAll(fromChildRead[i], primes.data(), count * sizeof(int));
        CloseHandle(fromChildRead[i]);

        WaitForSingleObject(pi[i].hProcess, INFINITE);

        std::cout << "Procesul " << i + 1 << " (PID " << pi[i].dwProcessId << "), interval ["
                  << i * RANGE + 1 << ", " << (i + 1) * RANGE << "] -> "
                  << count << " numere prime:\n";
        for (int k = 0; k < count; ++k)
            std::cout << primes[k] << (k + 1 < count ? " " : "\n");
        std::cout << "\n";
        total += count;

        CloseHandle(pi[i].hProcess);
        CloseHandle(pi[i].hThread);
    }

    std::cout << "Total numere prime pana la " << NUM_PROC * RANGE << ": " << total << "\n";
    return 0;
}