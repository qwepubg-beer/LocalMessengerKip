#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <stdbool.h>

#define PIPE_NAME "\\\\.\\pipe\\ChatPipe"
#define BUFFER_SIZE 1024

HANDLE hPipe;
CRITICAL_SECTION csConsole;
volatile BOOL connected = TRUE;
HANDLE hReadThread;

// Переменные для приёма файла
volatile BOOL receivingFile = FALSE;
FILE* fp = NULL;
char fileName[256] = { 0 };
HANDLE hFileEvent;   // событие завершения передачи файла

// Поток для приема сообщений
DWORD WINAPI ReceiveThread(LPVOID param) {
    char buffer[BUFFER_SIZE];
    DWORD bytesRead;

    while (connected) {
        memset(buffer, 0, BUFFER_SIZE);

        if (!ReadFile(hPipe, buffer, BUFFER_SIZE, &bytesRead, NULL)) {
            DWORD error = GetLastError();
            if (connected && error != ERROR_BROKEN_PIPE) {
                EnterCriticalSection(&csConsole);
                printf("\n[Система]: Ошибка чтения: %d\n", error);
                printf("[Система]: Соединение с сервером потеряно\n");
                LeaveCriticalSection(&csConsole);
            }
            connected = FALSE;
            break;
        }

        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';

            EnterCriticalSection(&csConsole);

            if (receivingFile) {
                // Режим получения файла
                if (strcmp(buffer, "FILE_END") == 0) {
                    if (fp != NULL) {
                        fclose(fp);
                        fp = NULL;
                        printf("\n[Система]: Файл %s успешно получен.\n", fileName);
                    }
                    else {
                        printf("\n[Система]: Получение файла прервано (не удалось создать локальный файл).\n");
                    }
                    receivingFile = FALSE;
                    SetEvent(hFileEvent);  // сигнал основному потоку
                }
                else {
                    // Пишем данные (если файл удалось открыть)
                    if (fp != NULL) {
                        fwrite(buffer, 1, bytesRead, fp);
                    }
                    // Иначе просто игнорируем данные до FILE_END
                }
            }
            else {
                // Обычный режим чата или служебные сообщения
                if (strncmp(buffer, "FILE_START:", 11) == 0) {
                    // Формат: FILE_START:имя_файла:размер
                    char* fname = buffer + 11;
                    char* colon = strchr(fname, ':');
                    if (colon != NULL) {
                        *colon = '\0';
                        long fileSize = atol(colon + 1);
                        strcpy(fileName, fname);
                        fp = fopen(fileName, "wb");
                        if (fp == NULL) {
                            printf("\n[Ошибка]: Не удалось создать файл %s\n", fileName);
                        }
                        receivingFile = TRUE;
                        // Основной поток ждёт SetEvent, который будет установлен при FILE_END
                    }
                }
                else if (strncmp(buffer, "FILE_ERROR:", 11) == 0) {
                    printf("\n[Система]: Ошибка получения файла: %s\n", buffer + 11);
                    SetEvent(hFileEvent);  // сигнал, что команда завершена с ошибкой
                }
                else {
                    // Обычное сообщение чата
                    printf("\n%s\n", buffer);
                    printf("Вы: ");
                    fflush(stdout);
                }
            }

            LeaveCriticalSection(&csConsole);
        }
    }

    return 0;
}

int main() {
    char buffer[BUFFER_SIZE];
    char userName[50];

    setlocale(LC_ALL, "rus");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);
    InitializeCriticalSection(&csConsole);

    // Событие для синхронизации при получении файла
    hFileEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (hFileEvent == NULL) {
        printf("Ошибка создания события\n");
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    printf("=====================================\n");
    printf("       Чат-клиент\n");
    printf("=====================================\n\n");

    // Ввод имени
    printf("Введите ваше имя: ");
    fgets(userName, sizeof(userName), stdin);
    userName[strcspn(userName, "\n")] = 0;
    userName[strcspn(userName, "\r")] = 0;

    if (strlen(userName) == 0) {
        strcpy(userName, "Аноним");
    }

    printf("\nПодключение к серверу...\n");

    // Ожидание сервера
    if (!WaitNamedPipe(TEXT(PIPE_NAME), 5000)) {
        printf("Ошибка: Сервер не запущен или не отвечает\n");
        printf("Убедитесь, что сервер запущен и повторите попытку\n");
        CloseHandle(hFileEvent);
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    // Подключение к серверу
    hPipe = CreateFile(
        TEXT(PIPE_NAME),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        printf("Ошибка подключения к серверу. Код: %d\n", GetLastError());
        CloseHandle(hFileEvent);
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    // Переводим канал в режим сообщений
    DWORD pipeMode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(hPipe, &pipeMode, NULL, NULL)) {
        printf("Ошибка установки режима канала\n");
        CloseHandle(hPipe);
        CloseHandle(hFileEvent);
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    // Отправляем имя серверу
    DWORD bytesWritten;
    if (!WriteFile(hPipe, userName, strlen(userName) + 1, &bytesWritten, NULL)) {
        printf("Ошибка отправки имени\n");
        CloseHandle(hPipe);
        CloseHandle(hFileEvent);
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    // Запускаем поток приема сообщений
    hReadThread = CreateThread(NULL, 0, ReceiveThread, NULL, 0, NULL);
    if (hReadThread == NULL) {
        printf("Ошибка создания потока приема\n");
        CloseHandle(hPipe);
        CloseHandle(hFileEvent);
        DeleteCriticalSection(&csConsole);
        return 1;
    }

    printf("\n=====================================\n");
    printf("Добро пожаловать в чат, %s!\n", userName);
    printf("=====================================\n");
    printf("Введите /quit для выхода\n");
    printf("Введите /takefile <путь> для получения файла с сервера\n");
    printf("=====================================\n\n");

    // Основной цикл отправки сообщений
    while (connected) {
        printf("Вы: ");
        fflush(stdout);

        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }

        // Удаляем символ новой строки
        buffer[strcspn(buffer, "\n")] = 0;
        buffer[strcspn(buffer, "\r")] = 0;

        if (strlen(buffer) == 0) {
            continue;
        }

        // Проверка на выход
        if (strcmp(buffer, "/quit") == 0) {
            printf("Выход из чата...\n");
            WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL);
            connected = FALSE;
            break;
        }

        // Проверка команды /takefile
        if (strncmp(buffer, "/takefile ", 10) == 0) {
            char fileCmd[BUFFER_SIZE];
            snprintf(fileCmd, BUFFER_SIZE, "takefile %s", buffer + 10);
            // Отправляем команду серверу
            if (!WriteFile(hPipe, fileCmd, strlen(fileCmd) + 1, &bytesWritten, NULL)) {
                printf("\n[Ошибка]: Не удалось отправить запрос файла\n");
                connected = FALSE;
                break;
            }
            printf("[Запрос]: Ожидание файла \"%s\"...\n", buffer + 10);
            // Ждём завершения передачи (успех или ошибка)
            DWORD waitResult = WaitForSingleObject(hFileEvent, 30000); // таймаут 30 сек
            if (waitResult == WAIT_TIMEOUT) {
                printf("[Ошибка]: Превышено время ожидания файла\n");
                receivingFile = FALSE;
                if (fp) { fclose(fp); fp = NULL; }
            }
            continue;  // переходим к следующей итерации (выведется "Вы: ")
        }

        // Отправляем обычное сообщение
        if (!WriteFile(hPipe, buffer, strlen(buffer) + 1, &bytesWritten, NULL)) {
            printf("\n[Ошибка]: Не удалось отправить сообщение\n");
            connected = FALSE;
            break;
        }
    }

    // Завершение работы
    connected = FALSE;

    if (hReadThread != NULL) {
        WaitForSingleObject(hReadThread, 2000);
        CloseHandle(hReadThread);
    }

    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
    }

    CloseHandle(hFileEvent);
    DeleteCriticalSection(&csConsole);
    printf("Соединение закрыто.\n");

    return 0;
}