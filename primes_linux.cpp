#include <iostream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <cerrno>
#include <cstdlib>

using namespace std;

const int LIMIT = 10000;
const int PROCESS_COUNT = 10;
const int RANGE_SIZE = 1000;

// Citeste exact numarul de bytes cerut.
bool readAll(int fd, void* buffer, size_t size) {
    char* ptr = static_cast<char*>(buffer);
    size_t total = 0;

    while (total < size) {
        ssize_t bytes = read(fd, ptr + total, size - total);

        if (bytes == 0)
            return false;

        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }

        total += static_cast<size_t>(bytes);
    }

    return true;
}

// Scrie exact numarul de bytes cerut.
bool writeAll(int fd, const void* buffer, size_t size) {
    const char* ptr = static_cast<const char*>(buffer);
    size_t total = 0;

    while (total < size) {
        ssize_t bytes = write(fd, ptr + total, size - total);

        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }

        total += static_cast<size_t>(bytes);
    }

    return true;
}

bool isPrime(int number) {
    if (number < 2)
        return false;

    for (int i = 2; i * i <= number; i++) {
        if (number % i == 0)
            return false;
    }

    return true;
}

void childProcess(int inputPipe, int outputPipe) {
    int start, end;

    // Primeste intervalul de la procesul parinte.
    if (!readAll(inputPipe, &start, sizeof(start)) ||
        !readAll(inputPipe, &end, sizeof(end))) {
        close(inputPipe);
        close(outputPipe);
        _exit(1);
    }

    close(inputPipe);

    // Calculeaza numerele prime din interval.
    vector<int> primes;

    for (int number = start; number <= end; number++) {
        if (isPrime(number))
            primes.push_back(number);
    }

    int count = static_cast<int>(primes.size());

    // Trimite numarul de prime si apoi valorile catre parinte.
    bool success = writeAll(outputPipe, &count, sizeof(count));

    if (success && count > 0) {
        success = writeAll(
            outputPipe,
            primes.data(),
            primes.size() * sizeof(int)
        );
    }

    close(outputPipe);
    _exit(success ? 0 : 1);
}

int main() {
    int inputPipes[PROCESS_COUNT][2];
    int outputPipes[PROCESS_COUNT][2];
    pid_t children[PROCESS_COUNT];

    // Creeaza cele 10 procese copil si pipe-urile lor.
    for (int i = 0; i < PROCESS_COUNT; i++) {
        if (pipe(inputPipes[i]) == -1 || pipe(outputPipes[i]) == -1) {
            perror("pipe");
            return 1;
        }

        children[i] = fork();

        if (children[i] < 0) {
            perror("fork");
            return 1;
        }

        if (children[i] == 0) {
            // Copilul inchide descriptorii mosteniti de la copiii anteriori.
            for (int j = 0; j < i; j++) {
                close(inputPipes[j][0]);
                close(inputPipes[j][1]);
                close(outputPipes[j][0]);
                close(outputPipes[j][1]);
            }

            // Copilul pastreaza doar capetele de pipe de care are nevoie.
            close(inputPipes[i][1]);
            close(outputPipes[i][0]);

            childProcess(inputPipes[i][0], outputPipes[i][1]);
        }

        // Parintele inchide capetele folosite doar de copilul curent.
        close(inputPipes[i][0]);
        close(outputPipes[i][1]);
    }

    // Trimite fiecarui copil intervalul sau.
    for (int i = 0; i < PROCESS_COUNT; i++) {
        int start = i * RANGE_SIZE + 1;
        int end = (i + 1) * RANGE_SIZE;

        bool success =
            writeAll(inputPipes[i][1], &start, sizeof(start)) &&
            writeAll(inputPipes[i][1], &end, sizeof(end));

        close(inputPipes[i][1]);

        if (!success) {
            cerr << "Eroare la trimiterea intervalului catre copilul "
                 << i + 1 << ".\n";
            return 1;
        }
    }

    // Primeste si afiseaza rezultatele fiecarui copil.
    int totalPrimes = 0;

    for (int i = 0; i < PROCESS_COUNT; i++) {
        int start = i * RANGE_SIZE + 1;
        int end = (i + 1) * RANGE_SIZE;
        int count = 0;

        cout << "Proces copil " << i + 1
             << " (intervalul " << start << "-" << end << "): ";

        bool success = readAll(
            outputPipes[i][0],
            &count,
            sizeof(count)
        );

        vector<int> primes;

        if (success && count > 0) {
            primes.resize(count);
            success = readAll(
                outputPipes[i][0],
                primes.data(),
                primes.size() * sizeof(int)
            );
        }

        close(outputPipes[i][0]);

        if (!success) {
            cerr << "\nEroare la primirea rezultatelor copilului "
                 << i + 1 << ".\n";
            return 1;
        }

        for (int prime : primes)
            cout << prime << " ";

        cout << "\n";
        totalPrimes += count;
    }

    // Asteapta terminarea tuturor proceselor copil.
    for (int i = 0; i < PROCESS_COUNT; i++) {
        waitpid(children[i], nullptr, 0);
    }

    cout << "\nTotal numere prime gasite: " << totalPrimes << "\n";

    return 0;
}

