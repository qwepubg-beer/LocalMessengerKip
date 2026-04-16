#include <windows.h>
#include <stdio.h>
#include <sddl.h>  // Добавлено для SDDL
#include <locale.h>
#define PIPE_NAME "\\\\10.37.216.74\\pipe\\ChatPipe"

int main() {
    HANDLE hPipe;
    char buffer[1024];
    DWORD bytesRead, bytesWritten;
    PSECURITY_DESCRIPTOR pSD = NULL;
    SECURITY_ATTRIBUTES sa;


    setlocale(LC_ALL, "Russian");           // Устанавливаем русскую локаль
    SetConsoleCP(1251);                      // Устанавливаем кодировку ввода (Windows-1251)
    SetConsoleOutputCP(1251);
    // Настройка безопасности для доступа без прав администратора
    const char* sddlString = "D:(A;;GA;;;AU)";
    
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
        sddlString,
        SDDL_REVISION_1,
        &pSD,
        NULL)) {
        printf("Ошибка создания дескриптора безопасности: %d\n", GetLastError());
        return 1;
    }

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;

    printf("Сервер запущен с открытым доступом для всех пользователей...\n");

    // Создание канала с настройками безопасности
    hPipe = CreateNamedPipe(
        TEXT(PIPE_NAME),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,  // Добавлен флаг
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        1024, 1024,
        0,
        &sa);

    LocalFree(pSD);  // Освобождаем память

    if (hPipe == INVALID_HANDLE_VALUE) {
        printf("CreateNamedPipe не удался. Ошибка: %d\n", GetLastError());
        return 1;
    }

    printf("Ожидание подключения клиента...\n");

    if (!ConnectNamedPipe(hPipe, NULL)) {
        printf("ConnectNamedPipe не удался. Ошибка: %d\n", GetLastError());
        CloseHandle(hPipe);
        return 1;
    }

    printf("Клиент подключился! Начинаем обмен сообщениями.\n");
    printf("=============================================\n");

    // Цикл обмена сообщениями (сервер: читает первым, потом отвечает)
    while (1) {
        // Чтение сообщения от клиента
        printf("Ожидание сообщения от клиента...\n");
        if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
            printf("Клиент отключился. Завершение работы сервера.\n");
            break;
        }
        buffer[bytesRead] = '\0';
        printf("Клиент: %s\n", buffer);

        // Отправка ответа клиенту
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