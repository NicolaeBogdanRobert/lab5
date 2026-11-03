// Cautarea numerelor prime pana la 10000 folosind 10 procese si pipe-uri (Linux)
// Compilare: g++ -o prime_linux prime_linux.cpp
// Rulare:    ./prime_linux

#include <iostream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>

const int NUM_PROC = 10;   // numarul de procese copil
const int RANGE    = 1000; // dimensiunea intervalului pentru fiecare proces

bool isPrime(int n) {
    if (n < 2) return false;
    for (int d = 2; d * d <= n; ++d)
        if (n % d == 0) return false;
    return true;
}

// Scrie exact len octeti in pipe
bool writeAll(int fd, const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    while (len > 0) {
        ssize_t w = write(fd, p, len);
        if (w <= 0) return false;
        p += w; len -= w;
    }
    return true;
}

// Citeste exact len octeti din pipe
bool readAll(int fd, void* buf, size_t len) {
    char* p = static_cast<char*>(buf);
    while (len > 0) {
        ssize_t r = read(fd, p, len);
        if (r <= 0) return false;
        p += r; len -= r;
    }
    return true;
}

int main() {
    int toChild[NUM_PROC][2];   // parinte -> copil (intervalul)
    int toParent[NUM_PROC][2];  // copil -> parinte (numerele prime)
    pid_t pids[NUM_PROC];

    std::cout << "Procesul initial (PID " << getpid() << ") creeaza "
              << NUM_PROC << " procese...\n\n";

    for (int i = 0; i < NUM_PROC; ++i) {
        if (pipe(toChild[i]) == -1 || pipe(toParent[i]) == -1) {
            perror("pipe");
            return 1;
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return 1;
        }

        if (pid == 0) {
            // ===== PROCES COPIL =====
            // inchidem capetele mostenite de la copiii anteriori
            for (int j = 0; j < i; ++j) {
                close(toChild[j][1]);
                close(toParent[j][0]);
            }
            close(toChild[i][1]);   // copilul doar citeste din toChild
            close(toParent[i][0]);  // copilul doar scrie in toParent

            int interval[2];
            if (!readAll(toChild[i][0], interval, sizeof(interval))) _exit(1);
            close(toChild[i][0]);

            std::vector<int> primes;
            for (int n = interval[0]; n <= interval[1]; ++n)
                if (isPrime(n)) primes.push_back(n);

            int count = static_cast<int>(primes.size());
            writeAll(toParent[i][1], &count, sizeof(count));
            if (count > 0)
                writeAll(toParent[i][1], primes.data(), count * sizeof(int));
            close(toParent[i][1]);
            _exit(0);
        }

        // ===== PROCES PARINTE =====
        pids[i] = pid;
        close(toChild[i][0]);
        close(toParent[i][1]);

        int interval[2] = { i * RANGE + 1, (i + 1) * RANGE };
        writeAll(toChild[i][1], interval, sizeof(interval));
        close(toChild[i][1]);
    }

    // Parintele primeste si afiseaza numerele prime de la fiecare proces
    int total = 0;
    for (int i = 0; i < NUM_PROC; ++i) {
        int count = 0;
        readAll(toParent[i][0], &count, sizeof(count));
        std::vector<int> primes(count);
        if (count > 0)
            readAll(toParent[i][0], primes.data(), count * sizeof(int));
        close(toParent[i][0]);
        waitpid(pids[i], nullptr, 0);

        std::cout << "Procesul " << i + 1 << " (PID " << pids[i] << "), interval ["
                  << i * RANGE + 1 << ", " << (i + 1) * RANGE << "] -> "
                  << count << " numere prime:\n";
        for (int k = 0; k < count; ++k)
            std::cout << primes[k] << (k + 1 < count ? " " : "\n");
        std::cout << "\n";
        total += count;
    }

    std::cout << "Total numere prime pana la " << NUM_PROC * RANGE << ": " << total << "\n";
    return 0;
}
