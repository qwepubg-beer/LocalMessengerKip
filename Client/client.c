#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <stdbool.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"

CRITICAL_SECTION csConsole;
volatile BOOL g_isConnected = TRUE;

DWORD WINAPI ReceiveThread(LPVOID param) {
    HANDLE hPipe = (HANDLE)param;
    char buffer[1024];
    DWORD bytesRead;

    while (g_isConnected) {
        if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
            EnterCriticalSection(&csConsole);
            printf("\n[Система]: Соединение с сервером потеряно.\n");
            LeaveCriticalSection(&csConsole);
            g_isConnected = FALSE;
            break;
        }
        buffer[bytesRead] = '\0';

        EnterCriticalSection(&csConsole);
        printf("\n%s\nВы: ", buffer);
        fflush(stdout);
        LeaveCriticalSection(&csConsole);
    }
    return 0;
}

int main() {
    HANDLE hPipe;
    char buffer[1024];
    DWORD bytesWritten;
    char userName[50];

    setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);
    InitializeCriticalSection(&csConsole);

    printf("=== Чат-клиент ===\n");
    printf("Введите ваше имя: ");
    fgets(userName, sizeof(userName), stdin);
    userName[strcspn(userName, "\r\n")] = 0;

    printf("Подключение к серверу...\n");
    if (!WaitNamedPipe(TEXT(PIPE_NAME), NMPWAIT_WAIT_FOREVER)) {
        printf("Ошибка: сервер не запущен. Код: %d\n", GetLastError());
        return 1;
    }

    hPipe = CreateFile(TEXT(PIPE_NAME), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPipe == INVALID_HANDLE_VALUE) {
        printf("Ошибка подключения. Код: %d\n", GetLastError());
        return 1;
    }

    // Отправляем имя серверу
    WriteFile(hPipe, userName, strlen(userName) + 1, &bytesWritten, NULL);

    // Запускаем поток чтения (передаём хэндл по значению)
    HANDLE hReadThread = CreateThread(NULL, 0, ReceiveThread, (LPVOID)hPipe, 0, NULL);

    EnterCriticalSection(&csConsole);
    printf("\n=== Добро пожаловать в чат, %s! ===\n", userName);
    printf("Вводите сообщения и нажимайте Enter. Для выхода введите /quit\n");
    printf("==================================\n");
    LeaveCriticalSection(&csConsole);

    // Основной цикл отправки
    while (g_isConnected) {
        printf("Вы: ");
        fflush(stdout);
        if (!fgets(buffer, sizeof(buffer), stdin)) break;
        buffer[strcspn(buffer, "\r\n")] = 0;
        if (strlen(buffer) == 0) continue;

        if (strcmp(buffer, "/quit") == 0) {
            WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL);
            g_isConnected = FALSE;
            break;
        }

        if (!WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL)) {
            EnterCriticalSection(&csConsole);
            printf("\nОшибка отправки: %d\n", GetLastError());
            LeaveCriticalSection(&csConsole);
            g_isConnected = FALSE;
            break;
        }
    }

    // Корректное завершение
    g_isConnected = FALSE;
    CloseHandle(hPipe); // Разблокирует ReadFile в потоке приёма
    WaitForSingleObject(hReadThread, 2000);
    CloseHandle(hReadThread);
    DeleteCriticalSection(&csConsole);

    printf("Соединение закрыто.\n");
    return 0;
}