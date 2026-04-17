#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <stdbool.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024

CRITICAL_SECTION csConsole;
volatile BOOL g_isConnected = TRUE;
HANDLE hPipeGlobal = INVALID_HANDLE_VALUE;

// Поток для получения сообщений от сервера
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
                printf("Нажмите Enter для выхода...\n");
                LeaveCriticalSection(&csConsole);
                g_isConnected = FALSE;
            }
            break;
        }

        buffer[bytesRead] = '\0';

        // Вывод полученного сообщения
        EnterCriticalSection(&csConsole);
        printf("\r%s\n", buffer);
        printf("Вы: ");
        fflush(stdout);
        LeaveCriticalSection(&csConsole);
    }

    return 0;
}

// Функция для отправки сообщения с ожиданием подтверждения
BOOL SendMessageWithWait(HANDLE hPipe, const char* message, DWORD timeout) {
    DWORD bytesWritten;

    // Отправляем сообщение
    if (!WriteFile(hPipe, message, strlen(message) + 1, &bytesWritten, NULL)) {
        return FALSE;
    }

    // Ожидаем небольшое время для обработки сервером
    Sleep(timeout);

    return TRUE;
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
    printf("        Чат-клиент v2.0\n");
    printf("=====================================\n\n");

    // Ввод имени пользователя
    printf("Введите ваше имя: ");
    fflush(stdin);
    fgets(userName, sizeof(userName), stdin);
    userName[strcspn(userName, "\r\n")] = 0;

    if (strlen(userName) == 0) {
        strcpy(userName, "Аноним");
    }

    printf("\nПодключение к серверу...\n");

    // Ожидание доступности сервера
    if (!WaitNamedPipe(TEXT(PIPE_NAME), 5000)) {
        printf("Ошибка: Сервер не отвечает. Убедитесь, что сервер запущен.\n");
        printf("Код ошибки: %d\n", GetLastError());
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    // Подключение к серверу
    hPipe = CreateFile(TEXT(PIPE_NAME),
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);

    if (hPipe == INVALID_HANDLE_VALUE) {
        printf("Ошибка подключения к серверу. Код: %d\n", GetLastError());
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    // Отправка имени серверу
    DWORD bytesWritten;
    if (!WriteFile(hPipe, userName, strlen(userName) + 1, &bytesWritten, NULL)) {
        printf("Ошибка отправки имени. Код: %d\n", GetLastError());
        CloseHandle(hPipe);
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    // Запуск потока приёма сообщений
    HANDLE hReadThread = CreateThread(NULL, 0, ReceiveThread, (LPVOID)hPipe, 0, NULL);
    if (hReadThread == NULL) {
        printf("Ошибка создания потока приёма\n");
        CloseHandle(hPipe);
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    printf("\n=====================================\n");
    printf("Добро пожаловать в чат, %s!\n", userName);
    printf("=====================================\n");
    printf("Команды:\n");
    printf("  /quit     - выход из чата\n");
    printf("  /help     - показать эту справку\n");
    printf("=====================================\n\n");

    // Основной цикл отправки сообщений
    while (g_isConnected) {
        printf("Вы: ");
        fflush(stdout);

        if (!fgets(buffer, sizeof(buffer), stdin)) {
            break;
        }

        buffer[strcspn(buffer, "\r\n")] = 0;

        if (strlen(buffer) == 0) {
            continue;
        }

        // Обработка команд
        if (strcmp(buffer, "/quit") == 0) {
            SendMessageWithWait(hPipe, buffer, 100);
            printf("Выход из чата...\n");
            g_isConnected = FALSE;
            break;
        }

        if (strcmp(buffer, "/help") == 0) {
            printf("\n=== Справка ===\n");
            printf("/quit - выход из чата\n");
            printf("/help - показать справку\n");
            printf("Любое другое сообщение будет отправлено в чат\n");
            printf("================\n\n");
            continue;
        }

        // Отправка сообщения с ожиданием обработки (100 мс)
        if (!SendMessageWithWait(hPipe, buffer, 100)) {
            EnterCriticalSection(&csConsole);
            printf("\n[Ошибка]: Не удалось отправить сообщение. Код: %d\n", GetLastError());
            printf("Соединение разорвано.\n");
            LeaveCriticalSection(&csConsole);
            g_isConnected = FALSE;
            break;
        }
    }

    // Корректное завершение
    printf("Завершение работы...\n");
    g_isConnected = FALSE;

    if (hPipe != INVALID_HANDLE_VALUE) {
        CancelIo(hPipe);  // Прерываем ожидающие операции ввода-вывода
        CloseHandle(hPipe);
    }

    if (hReadThread != NULL) {
        WaitForSingleObject(hReadThread, 2000);
        CloseHandle(hReadThread);
    }

    DeleteCriticalSection(&csConsole);
    printf("Соединение закрыто. До свидания!\n");

    return 0;
}