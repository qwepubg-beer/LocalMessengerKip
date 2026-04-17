#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"   // Для локального теста. Если сервер на другой машине — укажите \\\\имя_компьютера\\pipe\\ChatPipe
#define BUFFER_SIZE 1024

CRITICAL_SECTION csConsole;
volatile BOOL g_isConnected = TRUE;

DWORD WINAPI ReceiveThread(LPVOID param) {
    HANDLE hPipe = (HANDLE)param;
    char buffer[BUFFER_SIZE];
    DWORD bytesRead;

    while (g_isConnected) {
        memset(buffer, 0, BUFFER_SIZE);
        if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
            if (g_isConnected) {
                EnterCriticalSection(&csConsole);
                printf("\n[Система]: Соединение с сервером потеряно.\n");
                LeaveCriticalSection(&csConsole);
                g_isConnected = FALSE;
            }
            break;
        }
        buffer[bytesRead] = '\0';

        EnterCriticalSection(&csConsole);
        printf("\r%s\nВы: ", buffer);
        fflush(stdout);
        LeaveCriticalSection(&csConsole);
    }
    return 0;
}

int main() {
    HANDLE hPipe;
    char buffer[BUFFER_SIZE];
    char userName[50];

    setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);
    InitializeCriticalSection(&csConsole);

    printf("=====================================\n");
    printf("        Чат-клиент v2.1 (исправленный)\n");
    printf("=====================================\n\n");

    printf("Введите ваше имя: ");
    fgets(userName, sizeof(userName), stdin);
    userName[strcspn(userName, "\r\n")] = 0;
    if (strlen(userName) == 0) strcpy(userName, "Аноним");

    printf("\nПодключение к серверу...\n");

    if (!WaitNamedPipe(TEXT(PIPE_NAME), 10000)) {
        printf("Сервер не отвечает. Код ошибки: %d\n", GetLastError());
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    hPipe = CreateFile(TEXT(PIPE_NAME), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPipe == INVALID_HANDLE_VALUE) {
        printf("Ошибка подключения. Код: %d\n", GetLastError());
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    // Отправка имени
    WriteFile(hPipe, userName, strlen(userName) + 1, NULL, NULL);

    HANDLE hReadThread = CreateThread(NULL, 0, ReceiveThread, hPipe, 0, NULL);

    printf("\nДобро пожаловать, %s! Команды: /quit, /help\n\n", userName);

    while (g_isConnected) {
        printf("Вы: ");
        fflush(stdout);

        if (!fgets(buffer, sizeof(buffer), stdin)) break;
        buffer[strcspn(buffer, "\r\n")] = 0;

        if (strlen(buffer) == 0) continue;

        if (strcmp(buffer, "/quit") == 0) {
            WriteFile(hPipe, buffer, strlen(buffer) + 1, NULL, NULL);
            printf("Выход из чата...\n");
            g_isConnected = FALSE;
            break;
        }

        if (strcmp(buffer, "/help") == 0) {
            printf("\n=== Справка ===\n/quit - выход\n/help - справка\n================\n\n");
            continue;
        }

        WriteFile(hPipe, buffer, strlen(buffer) + 1, NULL, NULL);
    }

    g_isConnected = FALSE;
    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
    }
    if (hReadThread) {
        WaitForSingleObject(hReadThread, 2000);
        CloseHandle(hReadThread);
    }
    DeleteCriticalSection(&csConsole);
    printf("До свидания!\n");
    return 0;
}