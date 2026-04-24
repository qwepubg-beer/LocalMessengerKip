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

typedef struct {
    HANDLE hPipe;
    DWORD clientId;
    char name[50];
    HANDLE hThread;
    BOOL sendingFile;   // флаг: идёт передача файла этому клиенту
} CLIENT_INFO;

CLIENT_INFO* clients[MAX_CLIENTS];
int clientCount = 0;
CRITICAL_SECTION cs;

// Функция рассылки сообщений всем клиентам
void BroadcastMessage(const char* message, HANDLE excludePipe) {
    EnterCriticalSection(&cs);

    for (int i = 0; i < clientCount; i++) {
        if (clients[i] != NULL && clients[i]->hPipe != excludePipe) {
            // Не отправляем, если клиент занят приёмом файла
            if (clients[i]->sendingFile) {
                continue;
            }
            DWORD bytesWritten;
            WriteFile(clients[i]->hPipe, message, strlen(message) + 1, &bytesWritten, NULL);
        }
    }

    LeaveCriticalSection(&cs);
}

// Поток для обработки клиента
unsigned int __stdcall ClientThread(void* param) {
    CLIENT_INFO* client = (CLIENT_INFO*)param;
    HANDLE hPipe = client->hPipe;
    char buffer[BUFFER_SIZE];
    DWORD bytesRead;
    BOOL connected = TRUE;

    // Отправляем приветствие
    char welcome[BUFFER_SIZE];
    snprintf(welcome, BUFFER_SIZE, "Добро пожаловать в чат, %s!\n", client->name);
    WriteFile(hPipe, welcome, strlen(welcome) + 1, &bytesRead, NULL);

    // Оповещаем всех о входе нового пользователя
    char joinMsg[BUFFER_SIZE];
    snprintf(joinMsg, BUFFER_SIZE, "Система: %s подключился к чату", client->name);
    BroadcastMessage(joinMsg, hPipe);

    printf("[Система]: %s подключился. Всего клиентов: %d\n", client->name, clientCount);

    // Цикл приема сообщений
    while (connected) {
        memset(buffer, 0, BUFFER_SIZE);

        // Читаем сообщение от клиента
        if (!ReadFile(hPipe, buffer, BUFFER_SIZE, &bytesRead, NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_BROKEN_PIPE && error != ERROR_NO_DATA) {
                printf("[Ошибка]: ReadFile для %s, код: %d\n", client->name, error);
            }
            break;
        }

        if (bytesRead == 0) {
            break;
        }

        buffer[bytesRead] = '\0';

        // Проверка на выход
        if (strcmp(buffer, "/quit") == 0) {
            printf("[%s]: Пользователь вышел из чата\n", client->name);
            break;
        }

        // Проверка команды takefile
        if (strncmp(buffer, "takefile ", 9) == 0) {
            char* filepath = buffer + 9;
            while (*filepath == ' ') filepath++;  // пропуск начальных пробелов
            if (filepath[0] == '\0') {
                char* err = "FILE_ERROR:Путь к файлу не указан";
                WriteFile(hPipe, err, strlen(err) + 1, &bytesRead, NULL);
            }
            else {
                FILE* fp = fopen(filepath, "rb");
                if (fp == NULL) {
                    char errMsg[BUFFER_SIZE];
                    snprintf(errMsg, BUFFER_SIZE, "FILE_ERROR:Не удалось открыть файл %s", filepath);
                    WriteFile(hPipe, errMsg, strlen(errMsg) + 1, &bytesRead, NULL);
                }
                else {
                    // Входим в режим передачи файла
                    EnterCriticalSection(&cs);
                    client->sendingFile = TRUE;
                    LeaveCriticalSection(&cs);

                    // Определяем размер и имя файла
                    fseek(fp, 0, SEEK_END);
                    long fileSize = ftell(fp);
                    fseek(fp, 0, SEEK_SET);
                    const char* filename = strrchr(filepath, '\\');
                    if (filename != NULL) filename++;
                    else filename = filepath;

                    // Отправляем заголовок
                    char startMsg[BUFFER_SIZE];
                    snprintf(startMsg, BUFFER_SIZE, "FILE_START:%s:%ld", filename, fileSize);
                    if (!WriteFile(hPipe, startMsg, strlen(startMsg) + 1, &bytesRead, NULL)) {
                        fclose(fp);
                        EnterCriticalSection(&cs);
                        client->sendingFile = FALSE;
                        LeaveCriticalSection(&cs);
                        break;  // разрыв соединения
                    }

                    // Отправляем данные кусками
                    char dataBuffer[BUFFER_SIZE];
                    size_t bytes;
                    while ((bytes = fread(dataBuffer, 1, BUFFER_SIZE - 1, fp)) > 0) {
                        if (!WriteFile(hPipe, dataBuffer, bytes, &bytesRead, NULL)) {
                            break;
                        }
                    }
                    fclose(fp);

                    // Завершаем передачу
                    WriteFile(hPipe, "FILE_END", 8 + 1, &bytesRead, NULL); // "FILE_END\0"

                    // Выходим из режима передачи
                    EnterCriticalSection(&cs);
                    client->sendingFile = FALSE;
                    LeaveCriticalSection(&cs);

                    printf("[Файл]: %s отправлен клиенту %s\n", filename, client->name);
                }
            }
            continue;  // возвращаемся к приёму следующих сообщений
        }

        // Обычное сообщение чата
        if (strlen(buffer) > 0) {
            char formattedMsg[BUFFER_SIZE];
            snprintf(formattedMsg, BUFFER_SIZE, "%s: %s", client->name, buffer);
            printf("%s\n", formattedMsg);

            // Рассылаем всем клиентам
            EnterCriticalSection(&cs);
            for (int i = 0; i < clientCount; i++) {
                if (clients[i] != NULL) {
                    // Проверка на режим передачи файла (пропускаем занятых)
                    if (clients[i]->sendingFile) continue;
                    DWORD bytesWritten;
                    WriteFile(clients[i]->hPipe, formattedMsg, strlen(formattedMsg) + 1, &bytesWritten, NULL);
                }
            }
            LeaveCriticalSection(&cs);
        }
    }

    // Сохраняем имя перед удалением
    char leaveName[50];
    strcpy(leaveName, client->name);

    // Удаляем клиента из списка
    EnterCriticalSection(&cs);
    int index = -1;
    for (int i = 0; i < clientCount; i++) {
        if (clients[i] == client) {
            index = i;
            break;
        }
    }

    if (index != -1) {
        for (int j = index; j < clientCount - 1; j++) {
            clients[j] = clients[j + 1];
        }
        clientCount--;
    }
    LeaveCriticalSection(&cs);

    // Оповещаем всех о выходе (только не занятых передачей)
    char leaveMsg[BUFFER_SIZE];
    snprintf(leaveMsg, BUFFER_SIZE, "Система: %s покинул чат", leaveName);

    EnterCriticalSection(&cs);
    for (int i = 0; i < clientCount; i++) {
        if (clients[i] != NULL && !clients[i]->sendingFile) {
            DWORD bytesWritten;
            WriteFile(clients[i]->hPipe, leaveMsg, strlen(leaveMsg) + 1, &bytesWritten, NULL);
        }
    }
    LeaveCriticalSection(&cs);

    // Закрываем соединение
    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);

    printf("[Система]: %s отключился. Всего клиентов: %d\n", leaveName, clientCount);

    free(client);
    return 0;
}

int main() {
    HANDLE hPipe;
    PSECURITY_DESCRIPTOR pSD = NULL;
    SECURITY_ATTRIBUTES sa;

    setlocale(LC_ALL, "rus");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    InitializeCriticalSection(&cs);

    // Настройка безопасности для Windows
    const char* sddlString = "D:(A;;GA;;;AU)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddlString, SDDL_REVISION_1, &pSD, NULL)) {
        printf("Ошибка создания дескриптора безопасности: %d\n", GetLastError());
        DeleteCriticalSection(&cs);
        return 1;
    }

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;

    printf("=============================================\n");
    printf("Чат-сервер запущен (макс. %d клиентов)\n", MAX_CLIENTS);
    printf("=============================================\n\n");

    DWORD clientIdCounter = 0;

    // Основной цикл сервера
    while (1) {
        // Проверка лимита клиентов
        if (clientCount >= MAX_CLIENTS) {
            printf("[Система]: Достигнут лимит клиентов (%d). Ожидание...\n", MAX_CLIENTS);
            Sleep(1000);
            continue;
        }

        // Создание именованного канала
        hPipe = CreateNamedPipe(
            TEXT(PIPE_NAME),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            BUFFER_SIZE,
            BUFFER_SIZE,
            0,
            &sa
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            printf("Ошибка CreateNamedPipe: %d\n", GetLastError());
            Sleep(1000);
            continue;
        }

        printf("Ожидание подключения клиента...\n");

        // Ожидание подключения
        BOOL connected = ConnectNamedPipe(hPipe, NULL);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            printf("Ошибка ConnectNamedPipe: %d\n", GetLastError());
            CloseHandle(hPipe);
            continue;
        }

        printf("Клиент подключился! Получение имени...\n");

        // Чтение имени клиента
        char clientName[50];
        DWORD bytesRead;
        BOOL readResult = ReadFile(hPipe, clientName, sizeof(clientName), &bytesRead, NULL);

        if (!readResult || bytesRead == 0) {
            printf("Ошибка чтения имени клиента\n");
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            continue;
        }

        clientName[bytesRead] = '\0';
        clientName[strcspn(clientName, "\r\n")] = 0;

        if (strlen(clientName) == 0) {
            strcpy(clientName, "Аноним");
        }

        // Создание структуры клиента
        CLIENT_INFO* newClient = (CLIENT_INFO*)malloc(sizeof(CLIENT_INFO));
        newClient->hPipe = hPipe;
        newClient->clientId = ++clientIdCounter;
        strcpy(newClient->name, clientName);
        newClient->sendingFile = FALSE;   // инициализация флага

        // Добавление в список клиентов
        EnterCriticalSection(&cs);
        clients[clientCount++] = newClient;
        LeaveCriticalSection(&cs);

        // Создание потока для клиента
        newClient->hThread = (HANDLE)_beginthreadex(NULL, 0, ClientThread, newClient, 0, NULL);

        if (newClient->hThread == NULL) {
            printf("Ошибка создания потока для клиента\n");
            EnterCriticalSection(&cs);
            clientCount--;
            LeaveCriticalSection(&cs);
            free(newClient);
        }
    }

    DeleteCriticalSection(&cs);
    LocalFree(pSD);
    return 0;
}