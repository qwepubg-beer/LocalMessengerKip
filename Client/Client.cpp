#include <windows.h>
#include <stdio.h>

//#define PIPE_NAME "\\\\SERVER-PC\\pipe\\ChatPipe" // Имя должно совпадать с серверным
#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"
int main() {
    HANDLE hPipe;
    char buffer[1024];
    DWORD bytesRead, bytesWritten;

    printf("Клиент запущен. Подключение к серверу %s...\n", PIPE_NAME);

    // 1. Проверяем, доступен ли канал (ждем вечно)
    if (!WaitNamedPipe(TEXT(PIPE_NAME), NMPWAIT_WAIT_FOREVER)) {
        printf("WaitNamedPipe failed. Убедитесь, что сервер запущен. Error: %d\n", GetLastError());
        return 1;
    }

    // 2. Открываем канал
    hPipe = CreateFile(
        TEXT(PIPE_NAME),             // Имя канала
        GENERIC_READ | GENERIC_WRITE,// Чтение и запись
        0,                           // Делить нельзя
        NULL,                        // Атрибуты по умолчанию
        OPEN_EXISTING,               // Открыть существующий файл/канал
        0,                           // Атрибуты файла
        NULL);

    if (hPipe == INVALID_HANDLE_VALUE) {
        printf("CreateFile failed. Error: %d\n", GetLastError());
        return 1;
    }

    printf("Подключено к серверу! Начинаем обмен сообщениями.\n");
    printf("=============================================\n");

    // 3. Цикл обмена сообщениями
    while (1) {
        // --- Отправка сообщения серверу ---
        printf("Вы: ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0; // Убираем символ новой строки

        if (!WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL)) {
            printf("Не удалось отправить сообщение серверу. Ошибка: %d\n", GetLastError());
            break;
        }

        // --- Чтение ответа от сервера ---
        printf("Ожидание ответа от сервера...\n");
        if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
            printf("Сервер отключился. Завершение работы клиента.\n");
            break;
        }
        buffer[bytesRead] = '\0';
        printf("Сервер: %s\n", buffer);
    }

    // 4. Закрываем канал
    CloseHandle(hPipe);
    return 0;
}