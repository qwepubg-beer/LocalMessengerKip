#include <windows.h>
#include <stdio.h>
#include <locale.h>

#define PIPE_NAME "\\\\KB37-118-C01\\pipe\\ChatPipe"

int main() {
    HANDLE hPipe;
    char buffer[1024];
    DWORD bytesRead, bytesWritten;
    char userName[50];

    setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    printf("=== Многопользовательский чат-клиент ===\n");
    printf("Введите ваше имя: ");
    fgets(userName, sizeof(userName), stdin);
    userName[strcspn(userName, "\n")] = 0;

    printf("Подключение к серверу...\n");

    if (!WaitNamedPipe(TEXT(PIPE_NAME), NMPWAIT_WAIT_FOREVER)) {
        printf("WaitNamedPipe не удался. Ошибка: %d\n", GetLastError());
        return 1;
    }

    hPipe = CreateFile(
        TEXT(PIPE_NAME),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (hPipe == INVALID_HANDLE_VALUE) {
        printf("CreateFile не удался. Ошибка: %d\n", GetLastError());
        return 1;
    }

    printf("Подключено! Отправляем имя...\n");

    // Отправляем имя серверу
    WriteFile(hPipe, userName, strlen(userName) + 1, &bytesWritten, NULL);

    // Создаём поток для приёма сообщений
    HANDLE hReadThread = CreateThread(NULL, 0,
        [](LPVOID param) -> DWORD {
            HANDLE hPipe = *(HANDLE*)param;
            char buffer[1024];
            DWORD bytesRead;

            while (1) {
                if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
                    printf("\n[Система]: Соединение с сервером потеряно\n");
                    break;
                }
                buffer[bytesRead] = '\0';
                printf("\n%s\n", buffer);
                printf("Вы: ");
                fflush(stdout);
            }
            return 0;
        },
        &hPipe, 0, NULL);

    printf("\n=== Добро пожаловать в чат! ===\n");
    printf("Команды:\n");
    printf("  /list - показать всех участников\n");
    printf("  /w имя сообщение - личное сообщение\n");
    printf("  /quit - выход из чата\n");
    printf("================================\n\n");

    // Основной цикл отправки сообщений
    while (1) {
        printf("Вы: ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        if (strcmp(buffer, "/quit") == 0) {
            WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL);
            break;
        }

        if (!WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL)) {
            printf("Не удалось отправить сообщение. Ошибка: %d\n", GetLastError());
            break;
        }
    }

    CloseHandle(hReadThread);
    CloseHandle(hPipe);
    return 0;
}