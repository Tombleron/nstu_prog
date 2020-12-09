#include "server.c"
#include <stdio.h>
#include <string.h>
#include <windows.h>

#define SVCNAME "mySVC"

SERVICE_STATUS gSvcStatus;
SERVICE_STATUS_HANDLE gSvcStatusHandle;
HANDLE ghSvcStopEvent = NULL;

VOID SvcInstall(void);
VOID WINAPI SvcCtrlHandler(DWORD);
VOID WINAPI SvcMain(DWORD, LPTSTR *);

VOID ReportSvcStatus(DWORD, DWORD, DWORD);
VOID SvcInit(DWORD, LPTSTR *);
VOID SvcReportEvent(LPTSTR);

extern int Server();
extern int ServiceStart(int, char **);
extern void ServiceStop();

/* Входная точка процесса */
int main(int argc, char *argv[]) {
    // TODO: Расписать возможные аргументы
    if (argc < 2) {
        printf("Not enough arguments. See help for more information\n");
        return 1;
    }
    // TODO: Сделать help

    /* Если параметр коммандной строки "install", устанавливаем сервис
     * В другом случае сервис скорее всего уже был установлен */
    if (!strcmp(argv[1], "install")) {
        SvcInstall();
        return 1;
    }

    /* Таблица точек входа */
    SERVICE_TABLE_ENTRY DispatchTable[] = {
        {SVCNAME, (LPSERVICE_MAIN_FUNCTION)SvcMain}, {NULL, NULL}};

    if (!StartServiceCtrlDispatcher(DispatchTable)) {
        SvcReportEvent("StartServiceCtrlDispatcher");
    }
}

/* Функция установки сервиса в базу SCM */
void SvcInstall() {
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    TCHAR szPath[MAX_PATH];

    /* Получаем путь к исполняемому файлу текущего процесса */
    if (!GetModuleFileName(NULL, szPath, MAX_PATH)) {
        printf("Cannot install service (%ld)\n", GetLastError());
        return;
    }
    /* Получаем дескриптор в базу SCM */
    schSCManager = OpenSCManager(
        NULL, // NULL указывает на то, что это локальная машина
        NULL, // Здесть должен быть еще массив, но он нам не нужен
        SC_MANAGER_ALL_ACCESS); // Для получения полных прав доступа

    /* Проверяем успешность прошлой функции */
    if (NULL == schSCManager) {
        printf("OpenSCManager failed (%ld)\n", GetLastError());
        return;
    }

    /* Создаем сервис  */
    schService = CreateService(schSCManager, // SCM база
                               SVCNAME,      // Имя сервиса
                               SVCNAME, // Имя сервиса для отображения
                               SERVICE_ALL_ACCESS, // Желаемый доступ
                               SERVICE_WIN32_OWN_PROCESS, // Тип сервиса
                               SERVICE_DEMAND_START, // Тип запуска
                               SERVICE_ERROR_NORMAL, // Тип контроля ошибок
                               szPath, // Путь к исполняемому файлу сервиса
                               NULL, NULL, NULL, NULL, NULL);

    /* Проверяем успешность создания сервиса */
    if (schService == NULL) {
        printf("CreateService failed (%ld)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    } else {
        printf("Service installed successfully\n");
    }

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

/* Код сервиса */
void WINAPI SvcMain(DWORD dwArgc, LPSTR *lpszArgv) {
    char buf[256];

    gSvcStatusHandle = RegisterServiceCtrlHandler(SVCNAME, SvcCtrlHandler);
    if (!gSvcStatusHandle) {
        SvcReportEvent("Error registering\n");
        return;
    }

    gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gSvcStatus.dwServiceSpecificExitCode = 0;

    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 6000);
    SvcReportEvent("Starting service\n");

    // Отправляем сигнал о том, что сервер запущен
    int res = ServiceStart(dwArgc, lpszArgv);
    if (res) {
        sprintf(buf, "Error init server %d", res);
        SvcReportEvent(buf);
        SvcCtrlHandler(SERVICE_CONTROL_STOP);
        return;
    }

    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);
    SvcReportEvent("Service init\n");


    if (!Server()) {
        SvcReportEvent("Error staring server\n");
        ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
        SvcCtrlHandler(SERVICE_CONTROL_STOP);
        return;
    }
    return;
}

/* Установка текущего состояния сервиса и и отчет о нем SCM */
VOID ReportSvcStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode,
                     DWORD dwWaitHint) {

    static DWORD dwCheckPoint = 1;

    /* Заполняем поля структуры SERVICE_STATUS */
    if (dwCurrentState == SERVICE_START_PENDING) {
        gSvcStatus.dwControlsAccepted = 0;
    } else {
        gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    }

    gSvcStatus.dwCurrentState = dwCurrentState;
    gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    gSvcStatus.dwWaitHint = dwWaitHint;

    if ((dwCurrentState == SERVICE_RUNNING) ||
        (dwCurrentState == SERVICE_STOPPED)) {
        gSvcStatus.dwCheckPoint = 0;
    } else {
        gSvcStatus.dwCheckPoint = dwCheckPoint++;
    }

    // Рапортуем SCM текущий статус.
    SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

/* Эта функция вызывается SCM, когда она получает (что-то?) отправленно сервису
 * при помощи функции ControlService функцию */
void WINAPI SvcCtrlHandler(DWORD dwCtrl) {

    // Обрабатываем полученный код
    switch (dwCtrl) {
    case SERVICE_CONTROL_STOP:

        gSvcStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SvcReportEvent("Service stopping...");
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);

        ServiceStop();
        // Сигнализируем сервису остановиться
        ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);

        break;
    case SERVICE_CONTROL_INTERROGATE:
        ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);
        break;

    default:
        ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);
        break;
    }
}

/* Функция сохранения сообщейний в лог */
void SvcReportEvent(char *text) {

    DWORD res, Sz;
    HANDLE hFile;
    char buf[256];

    hFile = CreateFile("C:\\logfile.log", GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                       FILE_FLAG_WRITE_THROUGH, NULL);
    if (!hFile) {
        return;
    } else {
        GetFileSize(hFile, &Sz);
        SetFilePointer(hFile, 0, NULL, FILE_END);
        sprintf(buf, "%s\r\n", text);
        WriteFile(hFile, buf, strlen(buf), &res, NULL);
        CloseHandle(hFile);
        return;
    }
}
