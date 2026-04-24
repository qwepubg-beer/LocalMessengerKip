#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <sddl.h>
#include <locale.h>
#include <process.h>
#include <stdlib.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024
#define FILE_BUFFER_SIZE 8192
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

// Функция отправки файла клиенту
void SendFileToClient(HANDLE hPipe, const char* filename) {
    char filepath[512];
    char buffer[FILE_BUFFER_SIZE];
    DWORD bytesRead, bytesWritten;

    // Формируем путь к файлу в папке "files"
    snprintf(filepath, sizeof(filepath), "files\\%s", filename);

    // Проверяем существование файла
    if (GetFileAttributesA(filepath) == INVALID_FILE_ATTRIBUTES) {
        char errorMsg[BUFFER_SIZE];
        snprintf(errorMsg, sizeof(errorMsg), "Система: Файл '%s' не найден на сервере", filename);
        WriteFile(hPipe, errorMsg, strlen(errorMsg) + 1, &bytesWritten, NULL);
        printf("[Система]: Клиент запросил файл '%s' - файл не найден\n", filename);
        return;
    }

    // Открываем файл
    HANDLE hFile = CreateFileA(
        filepath,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        char errorMsg[BUFFER_SIZE];
        snprintf(errorMsg, sizeof(errorMsg), "Система: Ошибка открытия файла '%s' (код: %d)", filename, GetLastError());
        WriteFile(hPipe, errorMsg, strlen(errorMsg) + 1, &bytesWritten, NULL);
        printf("[Система]: Ошибка открытия файла '%s'\n", filename);
        return;
    }

    // Получаем размер файла
    DWORD fileSize = GetFileSize(hFile, NULL);

    // Отправляем команду начала передачи файла
    char fileHeader[BUFFER_SIZE];
    snprintf(fileHeader, sizeof(fileHeader), "FILE_START:%s:%u", filename, fileSize);
    WriteFile(hPipe, fileHeader, strlen(fileHeader) + 1, &bytesWritten, NULL);

    printf("[Система]: Начинается отправка файла '%s' (размер: %u байт)\n", filename, fileSize);

    // Отправляем файл блоками
    DWORD totalSent = 0;
    while (ReadFile(hFile, buffer, FILE_BUFFER_SIZE, &bytesRead, NULL) && bytesRead > 0) {
        // Отправляем блок с префиксом FILE_DATA
        char* dataBlock = (char*)malloc(bytesRead + 20);
        sprintf(dataBlock, "FILE_DATA:");
        memcpy(dataBlock + 10, buffer, bytesRead);

        if (!WriteFile(hPipe, dataBlock, bytesRead + 10, &bytesWritten, NULL)) {
            printf("[Система]: Ошибка отправки файла '%s'\n", filename);
            free(dataBlock);
            break;
        }

        totalSent += bytesRead;
        printf("[Система]: Отправлено %u / %u байт (%.1f%%)\r", totalSent, fileSize, (float)totalSent * 100 / fileSize);
        free(dataBlock);
    }

    printf("\n[Система]: Файл '%s' успешно отправлен клиенту\n", filename);

    // Отправляем команду завершения передачи
    char fileEnd[BUFFER_SIZE];
    snprintf(fileEnd, sizeof(fileEnd), "FILE_END:%s", filename);
    WriteFile(hPipe, fileEnd, strlen(fileEnd) + 1, &bytesWritten, NULL);

    CloseHandle(hFile);
}

// Функция рассылки сообщений всем клиентам
void BroadcastMessage(const char* message, HANDLE excludePipe) {
    EnterCriticalSection(&cs);

    for (int i = 0; i < clientCount; i++) {
        if (clients[i] != NULL && clients[i]->hPipe != excludePipe) {
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

    // Отправляем список доступных файлов
    char fileListMsg[BUFFER_SIZE];
    snprintf(fileListMsg, BUFFER_SIZE, "Система: Доступные команды: /quit - выход, takefile(имя_файла) - скачать файл");
    WriteFile(hPipe, fileListMsg, strlen(fileListMsg) + 1, &bytesRead, NULL);

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

        // Проверка на команду takefile
        if (strncmp(buffer, "takefile(", 9) == 0) {
            // Извлекаем имя файла
            char filename[256];
            int i, j = 0;
            for (i = 9; buffer[i] != '\0' && buffer[i] != ')' && j < 255; i++) {
                filename[j++] = buffer[i];
            }
            filename[j] = '\0';

            if (strlen(filename) > 0) {
                printf("[%s]: Запросил файл '%s'\n", client->name, filename);
                SendFileToClient(hPipe, filename);
            }
            else {
                char errorMsg[] = "Система: Неверный формат команды. Используйте: takefile(имя_файла)";
                WriteFile(hPipe, errorMsg, strlen(errorMsg) + 1, &bytesRead, NULL);
            }
            continue;
        }

        // Отправляем сообщение всем (включая отправителя для подтверждения)
        if (strlen(buffer) > 0) {
            char formattedMsg[BUFFER_SIZE];
            snprintf(formattedMsg, BUFFER_SIZE, "%s: %s", client->name, buffer);
            printf("%s\n", formattedMsg);

            // Рассылаем всем клиентам
            EnterCriticalSection(&cs);
            for (int i = 0; i < clientCount; i++) {
                if (clients[i] != NULL) {
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

    // Оповещаем всех о выходе
    char leaveMsg[BUFFER_SIZE];
    snprintf(leaveMsg, BUFFER_SIZE, "Система: %s покинул чат", leaveName);

    EnterCriticalSection(&cs);
    for (int i = 0; i < clientCount; i++) {
        if (clients[i] != NULL) {
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

    // Создаем папку для файлов если её нет
    CreateDirectoryA("files", NULL);

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
    printf("Файлы для скачивания должны быть в папке 'files/'\n");
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
        // Удаляем символы новой строки
        clientName[strcspn(clientName, "\r\n")] = 0;

        if (strlen(clientName) == 0) {
            strcpy(clientName, "Аноним");
        }

        // Создание структуры клиента
        CLIENT_INFO* newClient = (CLIENT_INFO*)malloc(sizeof(CLIENT_INFO));
        newClient->hPipe = hPipe;
        newClient->clientId = ++clientIdCounter;
        strcpy(newClient->name, clientName);

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