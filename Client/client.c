#include <windows.h>
#include <stdio.h>
#include <locale.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"  
#define BUFFER_SIZE 1024

int main() {
    setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    HANDLE hPipe;
    char buffer[1024];
    char clientName[64];
    DWORD bytesRead, bytesWritten;

    printf("Клиент запущен. Подключение к серверу...\n");

    // Ввод имени клиента
    printf("Введите ваше имя: ");
    fgets(clientName, sizeof(clientName), stdin);
    clientName[strcspn(clientName, "\n")] = 0;

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

    printf("Подключено к серверу!\n");
    printf("=============================================\n");

    // ОТПРАВЛЯЕМ ИМЯ КЛИЕНТА ПЕРВЫМ СООБЩЕНИЕМ
    if (!WriteFile(hPipe, clientName, strlen(clientName) + 1, &bytesWritten, NULL)) {
        printf("Не удалось отправить имя серверу. Ошибка: %d\n", GetLastError());
        CloseHandle(hPipe);
        return 1;
    }

    // Цикл обмена сообщениями
    while (1) {
        printf("Вы (%s): ", clientName);
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        // Форматируем сообщение с именем клиента
        char formatted[BUFFER_SIZE + 64];
        snprintf(formatted, sizeof(formatted), "%s: %s", clientName, buffer);

        if (!WriteFile(hPipe, formatted, strlen(formatted) + 1, &bytesWritten, NULL)) {
            printf("Не удалось отправить сообщение. Ошибка: %d\n", GetLastError());
            break;
        }

        printf("Ожидание ответа от сервера...\n");
        if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
            printf("Сервер отключился. Завершение работы клиента.\n");
            break;
        }
        buffer[bytesRead] = '\0';
        printf("%s\n", buffer);
    }

    CloseHandle(hPipe);
    return 0;
}