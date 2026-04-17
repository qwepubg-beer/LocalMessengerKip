#include <windows.h>
#include <stdio.h>
#include <sddl.h>
#include <locale.h>
#include <string.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"
#define MAX_CLIENTS 32

CRITICAL_SECTION csClients;

typedef struct {
    HANDLE hPipe;
    char username[64];
} ClientInfo;

ClientInfo clients[MAX_CLIENTS];
int numClients = 0;

/* Broadcast теперь принимает exceptPipe — чтобы не слать сообщение отправителю */
void BroadcastMessage(const char* msg, HANDLE exceptPipe) {
    EnterCriticalSection(&csClients);
    for (int i = 0; i < numClients; i++) {
        if (exceptPipe != NULL && clients[i].hPipe == exceptPipe) {
            continue;   /* пропускаем отправителя (он сам напечатает сообщение) */
        }
        DWORD bytesWritten;
        WriteFile(clients[i].hPipe, msg, (DWORD)strlen(msg) + 1, &bytesWritten, NULL);
        /* Ошибки игнорируем — клиент сам отвалится по ReadFile */
    }
    LeaveCriticalSection(&csClients);
}

DWORD WINAPI ClientHandler(LPVOID lpParam) {
    HANDLE hPipe = (HANDLE)lpParam;
    char buffer[1024];
    DWORD bytesRead;
    char username[64] = { 0 };

    /* 1. Читаем имя пользователя (первое сообщение от клиента) */
    if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
        CloseHandle(hPipe);
        return 0;
    }
    buffer[bytesRead] = '\0';
    strncpy(username, buffer, 63);
    username[63] = '\0';

    /* 2. Добавляем клиента в общий список */
    EnterCriticalSection(&csClients);
    if (numClients < MAX_CLIENTS) {
        clients[numClients].hPipe = hPipe;
        strcpy(clients[numClients].username, username);
        numClients++;
    }
    LeaveCriticalSection(&csClients);

    /* 3. Рассылаем событие «вошёл в чат» ВСЕМ (включая самого пользователя) */
    char eventMsg[256];
    sprintf(eventMsg, "Пользователь %s вошел в чат.", username);
    BroadcastMessage(eventMsg, NULL);
    printf("%s\n", eventMsg);

    /* 4. Основной цикл — читаем сообщения от клиента */
    while (1) {
        if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
            break;
        }
        buffer[bytesRead] = '\0';

        printf("%s: %s\n", username, buffer);

        /* Формируем сообщение для ВСЕХ, КРОМЕ отправителя */
        char fullMsg[1024 + 70];
        sprintf(fullMsg, "%s: %s", username, buffer);
        BroadcastMessage(fullMsg, hPipe);   /* exceptPipe = hPipe отправителя */
    }

    /* 5. Клиент отключился — удаляем из списка и уведомляем всех */
    EnterCriticalSection(&csClients);
    for (int i = 0; i < numClients; i++) {
        if (clients[i].hPipe == hPipe) {
            for (int j = i; j < numClients - 1; j++) {
                clients[j] = clients[j + 1];
            }
            numClients--;
            break;
        }
    }
    LeaveCriticalSection(&csClients);

    sprintf(eventMsg, "Пользователь %s вышел из чата.", username);
    BroadcastMessage(eventMsg, NULL);
    printf("%s\n", eventMsg);

    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    return 0;
}

int main() {
    setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    HANDLE hPipe;
    PSECURITY_DESCRIPTOR pSD = NULL;
    SECURITY_ATTRIBUTES sa;

    const char* sddlString = "D:(A;;GA;;;AU)";

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
        sddlString, SDDL_REVISION_1, &pSD, NULL)) {
        printf("Ошибка создания дескриптора безопасности: %d\n", GetLastError());
        return 1;
    }

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;

    InitializeCriticalSection(&csClients);

    printf("Сервер запущен (C, многопользовательский чат)...\n");

    /* Создаём ПЕРВУЮ инстанцию канала */
    hPipe = CreateNamedPipe(
        TEXT(PIPE_NAME),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        1024, 1024,
        0,
        &sa);

    if (hPipe == INVALID_HANDLE_VALUE) {
        printf("CreateNamedPipe не удался. Ошибка: %d\n", GetLastError());
        LocalFree(pSD);
        DeleteCriticalSection(&csClients);
        return 1;
    }

    DWORD dwThreadId;

    while (TRUE) {
        printf("Ожидание подключения клиента...\n");

        BOOL connected = ConnectNamedPipe(hPipe, NULL);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            printf("ConnectNamedPipe не удался. Ошибка: %d\n", GetLastError());
            break;
        }

        printf("Клиент подключился! Создаем поток-обработчик...\n");

        HANDLE hThread = CreateThread(NULL, 0, ClientHandler, hPipe, 0, &dwThreadId);
        if (hThread == NULL) {
            printf("Не удалось создать поток: %d\n", GetLastError());
        }
        else {
            CloseHandle(hThread);
        }

        /* Создаём новую инстанцию канала для следующего клиента */
        hPipe = CreateNamedPipe(
            TEXT(PIPE_NAME),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            1024, 1024,
            0,
            &sa);

        if (hPipe == INVALID_HANDLE_VALUE) {
            printf("CreateNamedPipe (новая инстанция) не удалась. Ошибка: %d\n", GetLastError());
            break;
        }
    }

    LocalFree(pSD);
    DeleteCriticalSection(&csClients);
    return 0;
}