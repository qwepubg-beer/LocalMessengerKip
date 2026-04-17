#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <sddl.h>
#include <locale.h>
#include <process.h>
#include <stdlib.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 5
#define MAX_QUEUE_SIZE 100

typedef struct {
    HANDLE hPipe;
    DWORD clientId;
    char name[50];
    HANDLE hThread;
} CLIENT_INFO;

typedef struct {
    char message[BUFFER_SIZE];
    CLIENT_INFO* sender;
} QUEUE_MESSAGE;

CLIENT_INFO* clients[MAX_CLIENTS];
int clientCount = 0;
CRITICAL_SECTION csClients;
CRITICAL_SECTION csConsole;
CRITICAL_SECTION csQueue;

QUEUE_MESSAGE messageQueue[MAX_QUEUE_SIZE];
int queueFront = 0, queueRear = 0, queueSize = 0;
HANDLE hQueueEvent;
HANDLE hBroadcastThread;
volatile BOOL serverRunning = TRUE;

void EnqueueMessage(const char* message, CLIENT_INFO* sender) {
    EnterCriticalSection(&csQueue);
    if (queueSize < MAX_QUEUE_SIZE) {
        strncpy(messageQueue[queueRear].message, message, BUFFER_SIZE - 1);
        messageQueue[queueRear].message[BUFFER_SIZE - 1] = '\0';
        messageQueue[queueRear].sender = sender;
        queueRear = (queueRear + 1) % MAX_QUEUE_SIZE;
        queueSize++;
        SetEvent(hQueueEvent);
    }
    LeaveCriticalSection(&csQueue);
}

BOOL DequeueMessage(QUEUE_MESSAGE* msg) {
    BOOL result = FALSE;
    EnterCriticalSection(&csQueue);
    if (queueSize > 0) {
        *msg = messageQueue[queueFront];
        queueFront = (queueFront + 1) % MAX_QUEUE_SIZE;
        queueSize--;
        result = TRUE;
    }
    LeaveCriticalSection(&csQueue);
    return result;
}

void BroadcastMessage(const char* message, CLIENT_INFO* sender) {
    EnterCriticalSection(&csClients);
    for (int i = 0; i < clientCount; i++) {
        if (clients[i] != NULL && clients[i] != sender) {
            DWORD bytesWritten;
            WriteFile(clients[i]->hPipe, message, strlen(message) + 1, &bytesWritten, NULL);
        }
    }
    LeaveCriticalSection(&csClients);
}

unsigned int __stdcall BroadcastThread(void* param) {
    QUEUE_MESSAGE msg;
    while (serverRunning) {
        WaitForSingleObject(hQueueEvent, 100);
        while (DequeueMessage(&msg)) {
            BroadcastMessage(msg.message, msg.sender);

            EnterCriticalSection(&csConsole);
            if (msg.sender) {
                printf("[%s]: %s\n", msg.sender->name, msg.message + strlen(msg.sender->name) + 2);
            }
            else {
                printf("%s\n", msg.message);
            }
            fflush(stdout);
            LeaveCriticalSection(&csConsole);
        }
    }
    return 0;
}

unsigned int __stdcall ClientThread(void* param) {
    CLIENT_INFO* client = (CLIENT_INFO*)param;
    char buffer[BUFFER_SIZE];
    DWORD bytesRead;

    // Приветствие
    char welcome[BUFFER_SIZE];
    snprintf(welcome, BUFFER_SIZE, "Добро пожаловать в чат, %s!\n", client->name);
    WriteFile(client->hPipe, welcome, strlen(welcome) + 1, &bytesRead, NULL);

    // Оповещение о входе
    char joinMsg[BUFFER_SIZE];
    snprintf(joinMsg, BUFFER_SIZE, "Система: %s подключился к чату", client->name);
    EnqueueMessage(joinMsg, NULL);

    while (serverRunning) {
        memset(buffer, 0, BUFFER_SIZE);
        if (!ReadFile(client->hPipe, buffer, BUFFER_SIZE, &bytesRead, NULL) || bytesRead == 0) {
            break;
        }
        buffer[bytesRead] = '\0';

        if (strcmp(buffer, "/quit") == 0) break;

        char formattedMsg[BUFFER_SIZE];
        snprintf(formattedMsg, BUFFER_SIZE, "%s: %s", client->name, buffer);
        EnqueueMessage(formattedMsg, client);

        Sleep(10);
    }

    // Удаление клиента
    char leaveName[50];
    strncpy(leaveName, client->name, 49); leaveName[49] = '\0';

    EnterCriticalSection(&csClients);
    for (int i = 0; i < clientCount; i++) {
        if (clients[i] == client) {
            for (int j = i; j < clientCount - 1; j++) clients[j] = clients[j + 1];
            clientCount--;
            break;
        }
    }
    LeaveCriticalSection(&csClients);

    char leaveMsg[BUFFER_SIZE];
    snprintf(leaveMsg, BUFFER_SIZE, "Система: %s покинул чат", leaveName);
    EnqueueMessage(leaveMsg, NULL);

    DisconnectNamedPipe(client->hPipe);
    CloseHandle(client->hPipe);

    EnterCriticalSection(&csConsole);
    printf("[Система]: %s отключился. Всего клиентов: %d\n", leaveName, clientCount);
    fflush(stdout);
    LeaveCriticalSection(&csConsole);

    free(client);
    return 0;
}

int main() {
    PSECURITY_DESCRIPTOR pSD = NULL;
    SECURITY_ATTRIBUTES sa;
    HANDLE hPipe;

    setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    InitializeCriticalSection(&csClients);
    InitializeCriticalSection(&csConsole);
    InitializeCriticalSection(&csQueue);

    hQueueEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    const char* sddlString = "D:(A;;GA;;;AU)";
    ConvertStringSecurityDescriptorToSecurityDescriptorA(sddlString, SDDL_REVISION_1, &pSD, NULL);

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;

    hBroadcastThread = (HANDLE)_beginthreadex(NULL, 0, BroadcastThread, NULL, 0, NULL);

    printf("=============================================\n");
    printf("Многопользовательский чат-сервер запущен\n");
    printf("Максимум клиентов: %d\n", MAX_CLIENTS);
    printf("=============================================\n\n");

    static DWORD clientIdCounter = 0;

    while (serverRunning) {
        if (clientCount >= MAX_CLIENTS) {
            EnterCriticalSection(&csConsole);
            printf("[Система]: Достигнут лимит клиентов (%d). Ожидание...\n", MAX_CLIENTS);
            fflush(stdout);
            LeaveCriticalSection(&csConsole);
            Sleep(1000);
            continue;
        }

        hPipe = CreateNamedPipe(TEXT(PIPE_NAME),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            BUFFER_SIZE, BUFFER_SIZE, 0, &sa);

        if (hPipe == INVALID_HANDLE_VALUE) {
            EnterCriticalSection(&csConsole);
            printf("[Ошибка]: CreateNamedPipe failed. Код: %d\n", GetLastError());
            fflush(stdout);
            LeaveCriticalSection(&csConsole);
            Sleep(1000);
            continue;
        }

        EnterCriticalSection(&csConsole);
        printf("[Система]: Ожидание подключения нового клиента...\n");
        fflush(stdout);
        LeaveCriticalSection(&csConsole);

        if (!ConnectNamedPipe(hPipe, NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_PIPE_CONNECTED) {
                CloseHandle(hPipe);
                continue;
            }
        }

        // === ИСПРАВЛЕНО: правильный размер буфера ===
        char clientName[50] = { 0 };
        DWORD bytesRead;
        if (!ReadFile(hPipe, clientName, sizeof(clientName), &bytesRead, NULL) || bytesRead == 0) {
            EnterCriticalSection(&csConsole);
            printf("[Ошибка]: Не удалось получить имя клиента\n");
            fflush(stdout);
            LeaveCriticalSection(&csConsole);
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            continue;
        }
        clientName[bytesRead] = '\0';
        clientName[strcspn(clientName, "\r\n")] = 0;

        CLIENT_INFO* newClient = (CLIENT_INFO*)malloc(sizeof(CLIENT_INFO));
        if (!newClient) {
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            continue;
        }

        newClient->hPipe = hPipe;
        newClient->clientId = ++clientIdCounter;
        strncpy(newClient->name, clientName, 49);
        newClient->name[49] = '\0';

        EnterCriticalSection(&csClients);
        clients[clientCount++] = newClient;
        LeaveCriticalSection(&csClients);

        // === Защищённый вывод ===
        EnterCriticalSection(&csConsole);
        printf("[Система]: %s подключился (ID: %d). Всего: %d\n",
            newClient->name, newClient->clientId, clientCount);
        fflush(stdout);
        LeaveCriticalSection(&csConsole);

        newClient->hThread = (HANDLE)_beginthreadex(NULL, 0, ClientThread, newClient, 0, NULL);
    }

    serverRunning = FALSE;
    WaitForSingleObject(hBroadcastThread, 3000);
    CloseHandle(hBroadcastThread);
    CloseHandle(hQueueEvent);
    DeleteCriticalSection(&csClients);
    DeleteCriticalSection(&csConsole);
    DeleteCriticalSection(&csQueue);
    if (pSD) LocalFree(pSD);

    return 0;
}