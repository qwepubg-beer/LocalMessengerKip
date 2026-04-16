#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <sddl.h>
#include <locale.h>
#include <process.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 5

typedef struct {
    HANDLE hPipe;
    DWORD clientId;
    char name[50];
    HANDLE hThread;
} CLIENT_INFO;

CLIENT_INFO* clients[MAX_CLIENTS];
int clientCount = 0;
CRITICAL_SECTION cs;

void BroadcastMessage(const char* sender, const char* message, HANDLE excludePipe) {
    char broadcastMsg[BUFFER_SIZE];
    snprintf(broadcastMsg, BUFFER_SIZE, "[%s]: %s", sender, message);

    EnterCriticalSection(&cs);
    for (int i = 0; i < clientCount; i++) {
        if (clients[i] != NULL && clients[i]->hPipe != excludePipe) {
            DWORD bytesWritten;
            // Игнорируем ошибки записи (клиент может отключиться)
            WriteFile(clients[i]->hPipe, broadcastMsg, strlen(broadcastMsg) + 1, &bytesWritten, NULL);
        }
    }
    LeaveCriticalSection(&cs);
}

unsigned int __stdcall ClientThread(void* param) {
    CLIENT_INFO* client = (CLIENT_INFO*)param;
    HANDLE hPipe = client->hPipe;
    char buffer[BUFFER_SIZE];
    DWORD bytesRead;

    // Приветствие
    char welcome[BUFFER_SIZE];
    snprintf(welcome, BUFFER_SIZE, "Добро пожаловать в чат, %s!\n", client->name);
    WriteFile(hPipe, welcome, strlen(welcome) + 1, &bytesRead, NULL);

    // Оповещение о входе
    char joinMsg[BUFFER_SIZE];
    snprintf(joinMsg, BUFFER_SIZE, "%s подключился к чату", client->name);
    BroadcastMessage("Система", joinMsg, hPipe);

    // Цикл приёма
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        if (!ReadFile(hPipe, buffer, BUFFER_SIZE, &bytesRead, NULL) || bytesRead == 0) {
            break;
        }
        buffer[bytesRead] = '\0';

        if (strcmp(buffer, "/quit") == 0) break;
        BroadcastMessage(client->name, buffer, hPipe);
    }

    // Сохраняем имя ДО освобождения памяти
    char leaveName[50];
    strncpy(leaveName, client->name, sizeof(leaveName));
    leaveName[49] = '\0';

    // Удаляем из списка
    EnterCriticalSection(&cs);
    for (int i = 0; i < clientCount; i++) {
        if (clients[i] == client) {
            for (int j = i; j < clientCount - 1; j++) {
                clients[j] = clients[j + 1];
            }
            clientCount--;
            break;
        }
    }
    LeaveCriticalSection(&cs);

    // Оповещение о выходе
    char leaveMsg[BUFFER_SIZE];
    snprintf(leaveMsg, BUFFER_SIZE, "%s покинул чат", leaveName);
    BroadcastMessage("Система", leaveMsg, NULL);

    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    free(client);

    EnterCriticalSection(&cs); // Защита printf от параллельных потоков
    printf("[Система]: %s отключился. Всего клиентов: %d\n", leaveName, clientCount);
    LeaveCriticalSection(&cs);
    return 0;
}

int main() {
    HANDLE hPipe;
    PSECURITY_DESCRIPTOR pSD = NULL;
    SECURITY_ATTRIBUTES sa;

    setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);
    InitializeCriticalSection(&cs);

    const char* sddlString = "D:(A;;GA;;;AU)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddlString, SDDL_REVISION_1, &pSD, NULL)) {
        printf("Ошибка создания дескриптора безопасности: %d\n", GetLastError());
        return 1;
    }

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;

    printf("Многопользовательский чат-сервер запущен (макс. %d клиентов)\n", MAX_CLIENTS);
    printf("=============================================\n");

    static DWORD clientIdCounter = 0;

    while (1) {
        if (clientCount >= MAX_CLIENTS) {
            printf("Отказ в подключении: достигнут лимит клиентов (%d).\n", MAX_CLIENTS);
            Sleep(1000);
            continue;
        }

        hPipe = CreateNamedPipe(TEXT(PIPE_NAME), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, BUFFER_SIZE, BUFFER_SIZE, 0, &sa);

        if (hPipe == INVALID_HANDLE_VALUE) {
            printf("CreateNamedPipe не удался. Ошибка: %d\n", GetLastError());
            Sleep(1000);
            continue;
        }

        printf("Ожидание подключения нового клиента...\n");
        if (!ConnectNamedPipe(hPipe, NULL)) {
            printf("ConnectNamedPipe не удался. Ошибка: %d\n", GetLastError());
            CloseHandle(hPipe);
            continue;
        }

        printf("Новый клиент подключился! Читаем имя...\n");
        char clientName[50];
        DWORD bytesRead;
        if (!ReadFile(hPipe, clientName, sizeof(clientName), &bytesRead, NULL) || bytesRead == 0) {
            printf("Не удалось получить имя клиента\n");
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            continue;
        }
        clientName[bytesRead] = '\0';
        clientName[strcspn(clientName, "\r\n")] = 0;

        CLIENT_INFO* newClient = (CLIENT_INFO*)malloc(sizeof(CLIENT_INFO));
        newClient->hPipe = hPipe;
        newClient->clientId = ++clientIdCounter;
        strncpy(newClient->name, clientName, 49);
        newClient->name[49] = '\0';

        EnterCriticalSection(&cs);
        clients[clientCount++] = newClient;
        LeaveCriticalSection(&cs);

        printf("[Система]: %s подключился. Всего клиентов: %d\n", newClient->name, clientCount);
        newClient->hThread = (HANDLE)_beginthreadex(NULL, 0, ClientThread, newClient, 0, NULL);
    }

    DeleteCriticalSection(&cs);
    LocalFree(pSD);
    return 0;
}