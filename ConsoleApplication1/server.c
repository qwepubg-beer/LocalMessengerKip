#include <windows.h>
#include <stdio.h>
#include <sddl.h>
#include <locale.h>
#include <process.h>  // Для _beginthreadex

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

// Структура для хранения информации о клиенте
typedef struct {
    HANDLE hPipe;
    DWORD clientId;
    char name[50];
    HANDLE hThread;
} CLIENT_INFO;

CLIENT_INFO clients[MAX_CLIENTS];
int clientCount = 0;
CRITICAL_SECTION cs;  // Для синхронизации доступа к общим данным

// Функция отправки сообщения всем клиентам
void BroadcastMessage(const char* sender, const char* message, HANDLE excludePipe) {
    EnterCriticalSection(&cs);
    for (int i = 0; i < clientCount; i++) {
        if (clients[i].hPipe != excludePipe) {
            char broadcastMsg[BUFFER_SIZE];
            snprintf(broadcastMsg, BUFFER_SIZE, "[%s]: %s", sender, message);

            DWORD bytesWritten;
            WriteFile(clients[i].hPipe, broadcastMsg, strlen(broadcastMsg) + 1, &bytesWritten, NULL);
        }
    }
    LeaveCriticalSection(&cs);
}

// Функция отправки личного сообщения
void SendPrivateMessage(const char* targetName, const char* sender, const char* message) {
    EnterCriticalSection(&cs);
    for (int i = 0; i < clientCount; i++) {
        if (strcmp(clients[i].name, targetName) == 0) {
            char privateMsg[BUFFER_SIZE];
            snprintf(privateMsg, BUFFER_SIZE, "[ЛС от %s]: %s", sender, message);

            DWORD bytesWritten;
            WriteFile(clients[i].hPipe, privateMsg, strlen(privateMsg) + 1, &bytesWritten, NULL);

            // Сообщение отправителю, что доставлено
            snprintf(privateMsg, BUFFER_SIZE, "[Система]: Сообщение для %s доставлено", targetName);
            WriteFile(clients[sender ? 1 : 0].hPipe, privateMsg, strlen(privateMsg) + 1, &bytesWritten, NULL);
            break;
        }
    }
    LeaveCriticalSection(&cs);
}

// Отправить список клиентов
void SendClientList(HANDLE hPipe) {
    EnterCriticalSection(&cs);
    char listMsg[BUFFER_SIZE] = "Подключенные клиенты:\n";

    for (int i = 0; i < clientCount; i++) {
        strcat(listMsg, "  - ");
        strcat(listMsg, clients[i].name);
        strcat(listMsg, "\n");
    }

    DWORD bytesWritten;
    WriteFile(hPipe, listMsg, strlen(listMsg) + 1, &bytesWritten, NULL);
    LeaveCriticalSection(&cs);
}

// Функция обработки одного клиента
unsigned int __stdcall ClientThread(void* param) {
    CLIENT_INFO* client = (CLIENT_INFO*)param;
    HANDLE hPipe = client->hPipe;
    char buffer[BUFFER_SIZE];
    DWORD bytesRead;

    // Приветственное сообщение
    char welcome[BUFFER_SIZE];
    snprintf(welcome, BUFFER_SIZE, "Добро пожаловать, %s!\nКоманды:\n  /list - список клиентов\n  /w имя сообщение - личное сообщение\n  /quit - выход\n", client->name);
    WriteFile(hPipe, welcome, strlen(welcome) + 1, &bytesRead, NULL);

    // Оповещаем всех о новом клиенте
    char joinMsg[BUFFER_SIZE];
    snprintf(joinMsg, BUFFER_SIZE, "%s подключился к чату", client->name);
    BroadcastMessage("Система", joinMsg, hPipe);

    // Цикл приёма сообщений от клиента
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);

        if (!ReadFile(hPipe, buffer, BUFFER_SIZE, &bytesRead, NULL) || bytesRead == 0) {
            break;
        }

        buffer[bytesRead] = '\0';

        // Обработка команд
        if (buffer[0] == '/') {
            if (strcmp(buffer, "/list") == 0 || strcmp(buffer, "/list\n") == 0) {
                SendClientList(hPipe);
            }
            else if (strncmp(buffer, "/w ", 3) == 0) {
                char targetName[50];
                char privateMsg[BUFFER_SIZE];
                if (sscanf(buffer + 3, "%49s %[^\n]", targetName, privateMsg) == 2) {
                    SendPrivateMessage(targetName, client->name, privateMsg);
                }
                else {
                    const char* errorMsg = "[Система]: Использование: /w имя сообщение\n";
                    WriteFile(hPipe, errorMsg, strlen(errorMsg) + 1, &bytesRead, NULL);
                }
            }
            else if (strcmp(buffer, "/quit") == 0 || strcmp(buffer, "/quit\n") == 0) {
                break;
            }
        }
        else {
            // Обычное сообщение - всем
            BroadcastMessage(client->name, buffer, hPipe);
        }
    }

    // Отключаем клиента
    EnterCriticalSection(&cs);
    for (int i = 0; i < clientCount; i++) {
        if (clients[i].clientId == client->clientId) {
            for (int j = i; j < clientCount - 1; j++) {
                clients[j] = clients[j + 1];
            }
            clientCount--;
            break;
        }
    }
    LeaveCriticalSection(&cs);

    // Оповещаем всех об отключении
    char leaveMsg[BUFFER_SIZE];
    snprintf(leaveMsg, BUFFER_SIZE, "%s покинул чат", client->name);
    BroadcastMessage("Система", leaveMsg, NULL);

    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    printf("[Система]: %s отключился. Всего клиентов: %d\n", client->name, clientCount);

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
    printf("Ожидание подключения клиентов...\n");
    printf("=============================================\n");

    static DWORD clientIdCounter = 0;

    // Основной цикл: принимаем новых клиентов
    while (1) {
        hPipe = CreateNamedPipe(
            TEXT(PIPE_NAME),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,  // Неограниченное количество клиентов
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
        // Удаляем символ новой строки если есть
        clientName[strcspn(clientName, "\n")] = 0;

        // Создаём структуру клиента
        CLIENT_INFO* newClient = (CLIENT_INFO*)malloc(sizeof(CLIENT_INFO));
        newClient->hPipe = hPipe;
        newClient->clientId = ++clientIdCounter;
        strncpy(newClient->name, clientName, 49);
        newClient->name[49] = '\0';

        // Добавляем в список
        EnterCriticalSection(&cs);
        clients[clientCount++] = *newClient;
        LeaveCriticalSection(&cs);

        printf("[Система]: %s подключился. Всего клиентов: %d\n", newClient->name, clientCount);

        // Запускаем поток для обработки клиента
        newClient->hThread = (HANDLE)_beginthreadex(NULL, 0, ClientThread, newClient, 0, NULL);
    }

    DeleteCriticalSection(&cs);
    return 0;
}