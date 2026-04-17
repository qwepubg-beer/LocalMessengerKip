#include <windows.h>
#include <stdio.h>
#include <sddl.h>
#include <locale.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024

int main() {
    HANDLE hPipe;
    char buffer[BUFFER_SIZE];
    char clientName[64] = "Гость";
    DWORD bytesRead, bytesWritten;
    PSECURITY_DESCRIPTOR pSD = NULL;
    SECURITY_ATTRIBUTES sa;

    setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    const char* sddlString = "D:(A;;GA;;;AU)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
        sddlString, SDDL_REVISION_1, &pSD, NULL)) {
        printf("Ошибка создания дескриптора безопасности: %d\n", GetLastError());
        return 1;
    }

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;

    printf("Сервер запущен с открытым доступом для всех пользователей...\n");

    hPipe = CreateNamedPipe(
        TEXT(PIPE_NAME),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        BUFFER_SIZE, BUFFER_SIZE,
        0,
        &sa);

    LocalFree(pSD);

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

    // Получаем имя клиента
    if (ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        strcpy_s(clientName, sizeof(clientName), buffer);
    }

    printf("Клиент '%s' подключился! Начинаем обмен сообщениями.\n", clientName);
    printf("=============================================\n");

    while (1) {
        printf("Ожидание сообщения от %s...\n", clientName);
        if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
            printf("Клиент отключился. Завершение работы сервера.\n");
            break;
        }
        buffer[bytesRead] = '\0';
        printf("%s\n", buffer);

        printf("Борисс: ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        char formatted[BUFFER_SIZE + 64];
        snprintf(formatted, sizeof(formatted), "Борисс: %s", buffer);

        if (!WriteFile(hPipe, formatted, strlen(formatted) + 1, &bytesWritten, NULL)) {
            printf("Не удалось отправить сообщение. Ошибка: %d\n", GetLastError());
            break;
        }
    }

    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    return 0;
}