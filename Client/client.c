#include <windows.h>
#include <stdio.h>
#include <locale.h>
#include <string.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"

DWORD WINAPI ClientReader(LPVOID lpParam) {
    HANDLE hPipe = (HANDLE)lpParam;
    char buffer[1024];
    DWORD bytesRead;

    while (1) {
        if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
            printf("\nСервер отключился. Завершение работы клиента.\n");
            break;
        }
        buffer[bytesRead] = '\0';
        printf("%s\n", buffer);   /* Выводим сообщения других пользователей + события */
    }
    return 0;
}

int main() {
    setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    HANDLE hPipe;
    char buffer[1024];
    char username[64];
    DWORD bytesWritten;

    /* Запрашиваем имя ПЕРЕД подключением к серверу */
    printf("Введите ваше имя: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;

    if (strlen(username) == 0) {
        printf("Имя не может быть пустым.\n");
        return 1;
    }

    printf("Клиент запущен. Подключение к серверу...\n");

    if (!WaitNamedPipe(TEXT(PIPE_NAME), NMPWAIT_WAIT_FOREVER)) {
        printf("WaitNamedPipe не удался. Убедитесь, что сервер запущен. Ошибка: %d\n", GetLastError());
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
        if (GetLastError() == 5) printf("Ошибка 5 = Отказано в доступе.\n");
        if (GetLastError() == 2) printf("Ошибка 2 = Канал не найден.\n");
        return 1;
    }

    printf("Подключено к серверу! Ваше имя: %s\n", username);
    printf("=============================================\n");

    /* Отправляем имя серверу (первое сообщение) */
    if (!WriteFile(hPipe, username, strlen(username) + 1, &bytesWritten, NULL)) {
        printf("Не удалось отправить имя. Ошибка: %d\n", GetLastError());
        CloseHandle(hPipe);
        return 1;
    }

    /* Запускаем отдельный поток для чтения сообщений */
    HANDLE hReadThread = CreateThread(NULL, 0, ClientReader, hPipe, 0, NULL);
    if (hReadThread == NULL) {
        printf("Не удалось создать поток чтения: %d\n", GetLastError());
        CloseHandle(hPipe);
        return 1;
    }
    CloseHandle(hReadThread);

    /* Основной цикл ввода сообщений */
    while (1) {
        printf("Вы: ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        if (strlen(buffer) == 0) continue;

        /* === ПЕЧАТАЕМ СВОЁ СООБЩЕНИЕ ЛОКАЛЬНО (сразу) === */
        char ownMsg[1024 + 70];
        sprintf(ownMsg, "%s: %s", username, buffer);
        printf("%s\n", ownMsg);

        /* === ОТПРАВЛЯЕМ ТОЛЬКО ТЕКСТ СООБЩЕНИЯ (без форматирования) === */
        if (!WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL)) {
            printf("Не удалось отправить сообщение. Сервер отключился.\n");
            break;
        }
    }

    CloseHandle(hPipe);
    return 0;
}