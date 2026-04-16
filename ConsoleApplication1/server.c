#include <windows.h>
#include <stdio.h>
#include <sddl.h>
#include <locale.h>
#include <process.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

// Структура для хранения информации о клиенте
typedef struct {
    HANDLE hPipe;
    DWORD clientId;
    char name[50];
    HANDLE hThread;
} CLIENT_INFO;

CLIENT_INFO* clients[MAX_CLIENTS];  // Массив УКАЗАТЕЛЕЙ на клиентов
int clientCount = 0;
CRITICAL_SECTION cs;

// Функция отправки сообщения всем клиентам
void BroadcastMessage(const char* sender, const char* message, HANDLE excludePipe) {
    char broadcastMsg[BUFFER_SIZE];
    snprintf(broadcastMsg, BUFFER_SIZE, "[%s]: %s", sender, message);

    EnterCriticalSection(&cs);
    for (int i = 0; i < clientCount; i++) {
        if (clients[i] != NULL && clients[i]->hPipe != excludePipe) {
            DWORD bytesWritten;
            WriteFile(clients[i]->hPipe, broadcastMsg, strlen(broadcastMsg) + 1, &bytesWritten, NULL);
        }
    }
    LeaveCriticalSection(&cs);
}

// Функция обработки клиента
unsigned int __stdcall ClientThread(void* param) {
    CLIENT_INFO* client = (CLIENT_INFO*)param;
    HANDLE hPipe = client->hPipe;
    char buffer[BUFFER_SIZE];
    DWORD bytesRead;

    // Приветствие
    char welcome[BUFFER_SIZE];
    snprintf(welcome, BUFFER_SIZE, "Добро пожаловать, %s!\n", client->name);
    WriteFile(hPipe, welcome, strlen(welcome) + 1, &bytesRead, NULL);

    // Оповещаем всех
    char joinMsg[BUFFER_SIZE];
    snprintf(joinMsg, BUFFER_SIZE, "%s подключился к чату", client->name);
    BroadcastMessage("Система", joinMsg, hPipe);

    // Цикл приёма сообщений
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);

        if (!ReadFile(hPipe, buffer, BUFFER_SIZE, &bytesRead, NULL) || bytesRead == 0) {
            break;
        }

        buffer[bytesRead] = '\0';

        // Обычное сообщение
        BroadcastMessage(client->name, buffer, hPipe);
    }

    // Отключение клиента
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

    // Оповещаем всех
    char leaveMsg[BUFFER_SIZE];
    snprintf(leaveMsg, BUFFER_SIZE, "%s покинул чат", client->name);
    BroadcastMessage("Система", leaveMsg, NULL);

    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    free(client);

    printf("[Система]: %s отключился. Всего клиентов: %d\n", client->name, clientCount);

    return 0;
}

int main() {
    HANDLE hPipe;
    PSECURITY_DESCRIPTOR pSD = NULL;
    SECURITY_ATTRIBUTES sa;

    setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    // Инициализация критической секции
    InitializeCriticalSection(&cs);

    // Настройка безопасности
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

    printf("Многопользовательский чат-сервер запущен\n");
    printf("=============================================\n");

    static DWORD clientIdCounter = 0;

    // Основной цикл: принимаем новых клиентов
    while (1) {
        // ВАЖНО: Убираем FILE_FLAG_FIRST_PIPE_INSTANCE при работе с несколькими клиентами
        hPipe = CreateNamedPipe(
            TEXT(PIPE_NAME),
            PIPE_ACCESS_DUPLEX,  // Убрали FILE_FLAG_FIRST_PIPE_INSTANCE
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            BUFFER_SIZE, BUFFER_SIZE,
            0,
            &sa);

        if (hPipe == INVALID_HANDLE_VALUE) {
            printf("CreateNamedPipe не удался. Ошибка: %d\n", GetLastError());
            continue;
        }

        printf("Ожидание подключения нового клиента...\n");

        if (!ConnectNamedPipe(hPipe, NULL)) {
            printf("ConnectNamedPipe не удался. Ошибка: %d\n", GetLastError());
            CloseHandle(hPipe);
            continue;
        }

        printf("Новый клиент подключился! Запрашиваем имя...\n");

        // Запрашиваем имя у клиента
        const char* nameRequest = "Введите ваше имя: ";
        DWORD bytesWritten;
        WriteFile(hPipe, nameRequest, strlen(nameRequest) + 1, &bytesWritten, NULL);

        char clientName[50];
        DWORD bytesRead;
        if (!ReadFile(hPipe, clientName, sizeof(clientName), &bytesRead, NULL)) {
            printf("Не удалось получить имя клиента\n");
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            continue;
        }

        clientName[bytesRead] = '\0';
        clientName[strcspn(clientName, "\n")] = 0;

        // Создаём структуру клиента
        CLIENT_INFO* newClient = (CLIENT_INFO*)malloc(sizeof(CLIENT_INFO));
        newClient->hPipe = hPipe;
        newClient->clientId = ++clientIdCounter;
        strncpy(newClient->name, clientName, 49);
        newClient->name[49] = '\0';

        // Добавляем в список (как указатель)
        EnterCriticalSection(&cs);
        clients[clientCount++] = newClient;
        LeaveCriticalSection(&cs);

        printf("[Система]: %s подключился. Всего клиентов: %d\n", newClient->name, clientCount);

        // Запускаем поток для обработки клиента
        newClient->hThread = (HANDLE)_beginthreadex(NULL, 0, ClientThread, newClient, 0, NULL);
    }

    DeleteCriticalSection(&cs);
    LocalFree(pSD);
    return 0;
}