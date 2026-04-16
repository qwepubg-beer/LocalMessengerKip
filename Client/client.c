#include <windows.h>
#include <stdio.h>
#include <locale.h>

#define PIPE_NAME "\\\\KB37-118-C01\\pipe\\ChatPipe"

DWORD WINAPI ReceiveThread(LPVOID param) {
    HANDLE hPipe = *(HANDLE*)param;
    char buffer[1024];
    DWORD bytesRead;

    while (ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        printf("\n%s\nВы: ", buffer);
        fflush(stdout);
    }

    printf("\n[Система]: Соединение с сервером потеряно\n");
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

    printf("Ваше имя: ");
    fgets(userName, sizeof(userName), stdin);
    userName[strcspn(userName, "\n")] = 0;

    if (!WaitNamedPipe(TEXT(PIPE_NAME), NMPWAIT_WAIT_FOREVER)) {
        printf("Ошибка: сервер не запущен\n");
        return 1;
    }

    hPipe = CreateFile(TEXT(PIPE_NAME), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPipe == INVALID_HANDLE_VALUE) {
        printf("Ошибка подключения: %d\n", GetLastError());
        return 1;
    }

    WriteFile(hPipe, userName, strlen(userName) + 1, &bytesWritten, NULL);

    HANDLE hReadThread = CreateThread(NULL, 0, ReceiveThread, &hPipe, 0, NULL);

    printf("\n=== Чат ===\nКоманды: /list, /w имя сообщение, /quit\n\n");

    while (1) {
        printf("Вы: ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL);

        if (strcmp(buffer, "/quit") == 0) break;
    }

    CloseHandle(hReadThread);
    CloseHandle(hPipe);
    return 0;
}