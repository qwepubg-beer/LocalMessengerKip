#include <windows.h>
#include <stdio.h>
#include <locale.h>
#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"

int main() {
    setlocale(LC_ALL, "Russian");           // Устанавливаем русскую локаль
    SetConsoleCP(1251);                      // Устанавливаем кодировку ввода (Windows-1251)
    SetConsoleOutputCP(1251);

    HANDLE hPipe;
    char buffer[1024];
    DWORD bytesRead, bytesWritten;

    printf("Клиент запущен. Подключение к серверу...\n");

    // Проверяем, доступен ли канал
    if (!WaitNamedPipe(TEXT(PIPE_NAME), NMPWAIT_WAIT_FOREVER)) {
        printf("WaitNamedPipe не удался. Убедитесь, что сервер запущен. Ошибка: %d\n", GetLastError());
        return 1;
    }

    // Открываем канал
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
        if (GetLastError() == 5) {
            printf("Ошибка 5 = Отказано в доступе. Проверьте настройки безопасности на сервере.\n");
        }
        if (GetLastError() == 2) {
            printf("Ошибка 2 = Канал не найден. Убедитесь, что сервер запущен.\n");
        }
        return 1;
    }

    printf("Подключено к серверу! Начинаем обмен сообщениями.\n");
    printf("=============================================\n");

    // Цикл обмена сообщениями (клиент: пишет первым, потом читает)
    while (1) {
        // Отправка сообщения серверу
        printf("Вы: ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        if (!WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL)) {
            printf("Не удалось отправить сообщение серверу. Ошибка: %d\n", GetLastError());
            break;
        }

        // Чтение ответа от сервера
        printf("Ожидание ответа от сервера...\n");
        if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
            printf("Сервер отключился. Завершение работы клиента.\n");
            break;
        }
        buffer[bytesRead] = '\0';
        printf("Сервер: %s\n", buffer);
    }

    CloseHandle(hPipe);
    return 0;
}