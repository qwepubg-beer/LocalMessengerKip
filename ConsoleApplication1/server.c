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
    HANDLE hWriteEvent;     // Событие для уведомления о готовности к записи
} CLIENT_INFO;

typedef struct {
    char message[BUFFER_SIZE];
    CLIENT_INFO* sender;
} QUEUE_MESSAGE;

CLIENT_INFO* clients[MAX_CLIENTS];
int clientCount = 0;
CRITICAL_SECTION csClients;      // Защита списка клиентов
CRITICAL_SECTION csConsole;       // Защита вывода в консоль
CRITICAL_SECTION csQueue;         // Защита очереди сообщений

QUEUE_MESSAGE messageQueue[MAX_QUEUE_SIZE];
int queueFront = 0;
int queueRear = 0;
int queueSize = 0;
HANDLE hQueueEvent;               // Событие для уведомления о новых сообщениях
HANDLE hBroadcastThread;          // Поток рассыльщика
volatile BOOL serverRunning = TRUE;

// Добавление сообщения в очередь
void EnqueueMessage(const char* message, CLIENT_INFO* sender) {
    EnterCriticalSection(&csQueue);

    if (queueSize < MAX_QUEUE_SIZE) {
        strncpy(messageQueue[queueRear].message, message, BUFFER_SIZE - 1);
        messageQueue[queueRear].message[BUFFER_SIZE - 1] = '\0';
        messageQueue[queueRear].sender = sender;
        queueRear = (queueRear + 1) % MAX_QUEUE_SIZE;
        queueSize++;
        SetEvent(hQueueEvent);  // Сигнализируем о новом сообщении
    }
    else {
        EnterCriticalSection(&csConsole);
        printf("[Ошибка]: Очередь сообщений переполнена!\n");
        LeaveCriticalSection(&csConsole);
    }

    LeaveCriticalSection(&csQueue);
}

// Извлечение сообщения из очереди
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

// Функция рассылки сообщений всем клиентам
void BroadcastMessage(const char* message, CLIENT_INFO* sender) {
    EnterCriticalSection(&csClients);

    for (int i = 0; i < clientCount; i++) {
        if (clients[i] != NULL && clients[i] != sender) {
            DWORD bytesWritten;
            BOOL writeResult = WriteFile(clients[i]->hPipe, message, strlen(message) + 1, &bytesWritten, NULL);

            if (!writeResult || bytesWritten == 0) {
                EnterCriticalSection(&csConsole);
                printf("[Система]: Ошибка отправки клиенту %s (код: %d)\n",
                    clients[i]->name, GetLastError());
                LeaveCriticalSection(&csConsole);
            }
        }
    }

    LeaveCriticalSection(&csClients);
}

// Поток для обработки очереди сообщений и рассылки
unsigned int __stdcall BroadcastThread(void* param) {
    QUEUE_MESSAGE msg;

    while (serverRunning) {
        // Ожидаем появления сообщений в очереди
        WaitForSingleObject(hQueueEvent, 100);

        while (DequeueMessage(&msg)) {
            // Рассылаем сообщение всем клиентам
            BroadcastMessage(msg.message, msg.sender);

            EnterCriticalSection(&csConsole);
            printf("[%s]: %s\n", msg.sender ? msg.sender->name : "Система",
                msg.message + (msg.sender ? strlen(msg.sender->name) + 3 : 0));
            LeaveCriticalSection(&csConsole);
        }
    }

    return 0;
}

// Поток для обработки отдельного клиента
unsigned int __stdcall ClientThread(void* param) {
    CLIENT_INFO* client = (CLIENT_INFO*)param;
    HANDLE hPipe = client->hPipe;
    char buffer[BUFFER_SIZE];
    DWORD bytesRead;

    // Приветственное сообщение
    char welcome[BUFFER_SIZE];
    snprintf(welcome, BUFFER_SIZE, "Добро пожаловать в чат, %s!\n", client->name);
    WriteFile(hPipe, welcome, strlen(welcome) + 1, &bytesRead, NULL);

    // Оповещение о входе
    char joinMsg[BUFFER_SIZE];
    snprintf(joinMsg, BUFFER_SIZE, "Система: %s подключился к чату", client->name);
    EnqueueMessage(joinMsg, NULL);

    // Цикл приёма сообщений от клиента
    while (serverRunning) {
        memset(buffer, 0, BUFFER_SIZE);

        // Читаем сообщение от клиента
        if (!ReadFile(hPipe, buffer, BUFFER_SIZE, &bytesRead, NULL) || bytesRead == 0) {
            break;
        }

        buffer[bytesRead] = '\0';

        // Проверка на выход
        if (strcmp(buffer, "/quit") == 0) {
            break;
        }

        // Формируем сообщение для рассылки
        char formattedMsg[BUFFER_SIZE];
        snprintf(formattedMsg, BUFFER_SIZE, "%s: %s", client->name, buffer);

        // Отправляем в очередь на рассылку
        EnqueueMessage(formattedMsg, client);

        // Небольшая задержка для имитации обработки очереди
        Sleep(10);
    }

    // Сохраняем имя для уведомления
    char leaveName[50];
    strncpy(leaveName, client->name, sizeof(leaveName) - 1);
    leaveName[49] = '\0';

    // Удаляем клиента из списка
    EnterCriticalSection(&csClients);
    for (int i = 0; i < clientCount; i++) {
        if (clients[i] == client) {
            for (int j = i; j < clientCount - 1; j++) {
                clients[j] = clients[j + 1];
            }
            clientCount--;
            break;
        }
    }
    LeaveCriticalSection(&csClients);

    // Оповещение о выходе
    char leaveMsg[BUFFER_SIZE];
    snprintf(leaveMsg, BUFFER_SIZE, "Система: %s покинул чат", leaveName);
    EnqueueMessage(leaveMsg, NULL);

    // Закрываем соединение
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);

    EnterCriticalSection(&csConsole);
    printf("[Система]: %s отключился. Всего клиентов: %d\n", leaveName, clientCount);
    LeaveCriticalSection(&csConsole);

    free(client);
    return 0;
}

int main() {
    HANDLE hPipe;
    PSECURITY_DESCRIPTOR pSD = NULL;
    SECURITY_ATTRIBUTES sa;

    setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    // Инициализация критических секций
    InitializeCriticalSection(&csClients);
    InitializeCriticalSection(&csConsole);
    InitializeCriticalSection(&csQueue);

    // Инициализация событий
    hQueueEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (hQueueEvent == NULL) {
        printf("Ошибка создания события: %d\n", GetLastError());
        return 1;
    }

    // Настройка безопасности
    const char* sddlString = "D:(A;;GA;;;AU)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddlString, SDDL_REVISION_1, &pSD, NULL)) {
        printf("Ошибка создания дескриптора безопасности: %d\n", GetLastError());
        return 1;
    }

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;

    // Запуск потока рассылки
    hBroadcastThread = (HANDLE)_beginthreadex(NULL, 0, BroadcastThread, NULL, 0, NULL);
    if (hBroadcastThread == NULL) {
        printf("Ошибка создания потока рассылки\n");
        return 1;
    }

    printf("=============================================\n");
    printf("Многопользовательский чат-сервер запущен\n");
    printf("Максимум клиентов: %d\n", MAX_CLIENTS);
    printf("Очередь сообщений: %d\n", MAX_QUEUE_SIZE);
    printf("=============================================\n\n");

    static DWORD clientIdCounter = 0;

    // Основной цикл принятия новых клиентов
    while (serverRunning) {
        // Проверка лимита клиентов
        if (clientCount >= MAX_CLIENTS) {
            EnterCriticalSection(&csConsole);
            printf("[Система]: Отказ в подключении - достигнут лимит клиентов (%d)\n", MAX_CLIENTS);
            LeaveCriticalSection(&csConsole);
            Sleep(1000);
            continue;
        }

        // Создание именованного канала
        hPipe = CreateNamedPipe(TEXT(PIPE_NAME),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            BUFFER_SIZE, BUFFER_SIZE, 0, &sa);

        if (hPipe == INVALID_HANDLE_VALUE) {
            EnterCriticalSection(&csConsole);
            printf("[Ошибка]: CreateNamedPipe не удался. Код: %d\n", GetLastError());
            LeaveCriticalSection(&csConsole);
            Sleep(1000);
            continue;
        }

        EnterCriticalSection(&csConsole);
        printf("[Система]: Ожидание подключения нового клиента...\n");
        LeaveCriticalSection(&csConsole);

        // Ожидание подключения клиента
        if (!ConnectNamedPipe(hPipe, NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_PIPE_CONNECTED) {
                EnterCriticalSection(&csConsole);
                printf("[Ошибка]: ConnectNamedPipe не удался. Код: %d\n", error);
                LeaveCriticalSection(&csConsole);
            }
            CloseHandle(hPipe);
            continue;
        }

        // Чтение имени клиента
        char clientName[50];
        DWORD bytesRead;
        if (!ReadFile(hPipe, clientName, sizeof(clientName), &bytesRead, NULL) || bytesRead == 0) {
            EnterCriticalSection(&csConsole);
            printf("[Ошибка]: Не удалось получить имя клиента\n");
            LeaveCriticalSection(&csConsole);
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            continue;
        }
        clientName[bytesRead] = '\0';
        clientName[strcspn(clientName, "\r\n")] = 0;

        // Создание структуры клиента
        CLIENT_INFO* newClient = (CLIENT_INFO*)malloc(sizeof(CLIENT_INFO));
        if (newClient == NULL) {
            printf("[Ошибка]: Не удалось выделить память для клиента\n");
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            continue;
        }

        newClient->hPipe = hPipe;
        newClient->clientId = ++clientIdCounter;
        strncpy(newClient->name, clientName, 49);
        newClient->name[49] = '\0';

        // Добавление в список клиентов
        EnterCriticalSection(&csClients);
        clients[clientCount++] = newClient;
        LeaveCriticalSection(&csClients);

        EnterCriticalSection(&csConsole);
        printf("[Система]: %s подключился (ID: %d). Всего клиентов: %d\n",
            newClient->name, newClient->clientId, clientCount);
        LeaveCriticalSection(&csConsole);

        // Запуск потока клиента
        newClient->hThread = (HANDLE)_beginthreadex(NULL, 0, ClientThread, newClient, 0, NULL);
        if (newClient->hThread == NULL) {
            EnterCriticalSection(&csConsole);
            printf("[Ошибка]: Не удалось создать поток для клиента %s\n", newClient->name);
            LeaveCriticalSection(&csConsole);

            // Удаляем клиента из списка
            EnterCriticalSection(&csClients);
            clientCount--;
            LeaveCriticalSection(&csClients);

            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            free(newClient);
        }
    }

    // Очистка ресурсов
    serverRunning = FALSE;
    WaitForSingleObject(hBroadcastThread, 3000);
    CloseHandle(hBroadcastThread);
    CloseHandle(hQueueEvent);
    DeleteCriticalSection(&csClients);
    DeleteCriticalSection(&csConsole);
    DeleteCriticalSection(&csQueue);
    LocalFree(pSD);

    return 0;
}