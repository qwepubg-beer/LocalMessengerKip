#include <windows.h>
#include <stdio.h>
#include <locale.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"

DWORD WINAPI ReceiveThread(LPVOID param) {
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
}

int main() {
    HANDLE hPipe;
    char buffer[1024];
    DWORD bytesWritten;
    char userName[50];

    setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    printf("=== Чат-клиент ===\n");
    printf("Введите ваше имя: ");
    fgets(userName, sizeof(userName), stdin);
    userName[strcspn(userName, "\n")] = 0;

    printf("Подключение к серверу...\n");

    if (!WaitNamedPipe(TEXT(PIPE_NAME), NMPWAIT_WAIT_FOREVER)) {
        printf("Ошибка: сервер не запущен. Код: %d\n", GetLastError());
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
        printf("Ошибка подключения. Код: %d\n", GetLastError());
        if (GetLastError() == 5) {
            printf("Нет доступа. Запустите сервер с правильными правами.\n");
        }
        return 1;
    }

    // Отправляем имя
    WriteFile(hPipe, userName, strlen(userName) + 1, &bytesWritten, NULL);

    // Поток для приёма сообщений
    HANDLE hReadThread = CreateThread(NULL, 0, ReceiveThread, &hPipe, 0, NULL);

    printf("\n=== Добро пожаловать в чат, %s! ===\n", userName);
    printf("Просто вводите сообщения и нажимайте Enter\n");
    printf("Для выхода введите /quit\n");
    printf("==================================\n\n");

    // Основной цикл отправки
    while (1) {
        printf("Вы: ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        if (strcmp(buffer, "/quit") == 0) {
            WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL);
            break;
        }

        if (!WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL)) {
            printf("Ошибка отправки: %d\n", GetLastError());
            break;
        }
    }

    CloseHandle(hReadThread);
    CloseHandle(hPipe);
    return 0;
}