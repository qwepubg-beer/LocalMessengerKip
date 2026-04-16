#include <windows.h>
#include <stdio.h>

//#define PIPE_NAME "\\\\SERVER-PC\\pipe\\ChatPipe"
#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"
// Укажите ИМЯ или IP вашего сервера
// Для теста на одном компьютере используйте:
// #define PIPE_NAME "\\\\.\\pipe\\ChatPipe"

int main() {
    HANDLE hPipe;
    char buffer[1024];
    DWORD bytesRead, bytesWritten;
    hPipe = CreateNamedPipe(
        TEXT(PIPE_NAME),             // Имя канала
        PIPE_ACCESS_DUPLEX,          // Дуплексный режим
        PIPE_TYPE_MESSAGE |          // Режим передачи сообщений
        PIPE_READMODE_MESSAGE |      // Режим чтения сообщений
        PIPE_WAIT,                   // Синхронный режим
        1,                           // Максимум 1 клиент
        1024, 1024,                  // Размеры буфера
        0,                           // Таймаут по умолчанию
        NULL);                       // Атрибуты безопасности

    if (hPipe == INVALID_HANDLE_VALUE) {
        printf("CreateNamedPipe failed. Error: %d\n", GetLastError());
        return 1;
    }

    printf("Сервер запущен. Ожидание подключения клиента...\n");
    if (!ConnectNamedPipe(hPipe, NULL)) {
        printf("ConnectNamedPipe failed. Error: %d\n", GetLastError());
        CloseHandle(hPipe);
        return 1;
    }
    printf("Клиент подключился! Начинаем обмен сообщениями.\n");
    printf("=============================================\n");
    while (1) {
        printf("Ожидание сообщения от клиента...\n");
        if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
            printf("Клиент отключился. Завершение работы сервера.\n");
            break;
        }
        buffer[bytesRead] = '\0'; 
        printf("Клиент: %s\n", buffer);
        printf("Вы: ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0;
        if (!WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL)) {
            printf("Не удалось отправить сообщение клиенту. Ошибка: %d\n", GetLastError());
            break;
        }
    }
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    return 0;
}

