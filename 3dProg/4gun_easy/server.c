#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

struct IDK {
  HANDLE forwardRead;
  HANDLE forwardWrite;
  HANDLE backwardRead;
  HANDLE backwardWrite;
  STARTUPINFO startInfo;
  PROCESS_INFORMATION procInfo;
};

int main(int argc, LPTSTR argv[]) {
  DWORD cbWritten, cbRead;
  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

  struct IDK arr[argc - 2];

  if (argc < 3) {
    printf("There must be at least two arguments\n");
    return -1;
  }
  int i = 0;
  for (i = 0; i < (argc - 2); i++) {
    struct IDK buffer;
    ZeroMemory(&buffer.procInfo, sizeof(buffer.procInfo));
    GetStartupInfo(&buffer.startInfo);

    CreatePipe(&buffer.forwardRead, &buffer.forwardWrite, &sa, 0);
    CreatePipe(&buffer.backwardRead, &buffer.backwardWrite, &sa, 0);

    buffer.startInfo.hStdInput = buffer.forwardRead;
    buffer.startInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    buffer.startInfo.hStdOutput = buffer.backwardWrite;
    buffer.startInfo.dwFlags = STARTF_USESTDHANDLES;

    arr[i] = buffer;

    if (CreateProcess(NULL, "a.exe", NULL, NULL, TRUE, 0, NULL, NULL,
                      &buffer.startInfo, &buffer.procInfo)) {
      printf("Process %lu started\n", buffer.procInfo.dwProcessId);
    } else {
      printf("CreateProcess failed. Error: %lu\n", GetLastError());
      return -2;
    }
  }

  /* Отправить имя файла в канал. */
  int done = 0, jobs = (argc - 2), changes = 0;
  char buffer[256];
  while (done != argc - 2) {
    for (int i = 0; i < argc - 2; i++) {
      if (ReadFile(arr[i].backwardRead, buffer, 256, &cbRead, NULL)) {
        printf("Received command: %c\n", buffer[0]);
        switch (buffer[0]) {
          case 'n':
            WriteFile(arr[i].forwardWrite, argv[argc - jobs],
                      strlen(argv[argc - jobs]) + 1, &cbWritten, NULL);
            jobs--;
            break;
          case 'c':
            WriteFile(arr[i].forwardWrite, argv[1], strlen(argv[1]) + 1,
                      &cbWritten, NULL);
            break;
          case 'p':
            WriteFile(arr[i].forwardWrite, "p", 2, &cbWritten, NULL);
            changes += atoi(buffer + 1);
            done++;
            break;
          default:
            printf("Unknown command\n");
            break;
        }
      } else {
        printf("Error reading %lu\n", GetLastError());
      }
    }
  }

  DWORD finish;
  for (i = 0; i < (argc - 2); i++) {
    finish = WaitForSingleObject(arr[i].procInfo.hProcess, INFINITE);
    if (finish == WAIT_OBJECT_0) {
      printf("Process %lu finished his work\n", arr[i].procInfo.dwProcessId);
    } else {
      printf("Process %lu failed his job\n", arr[i].procInfo.dwProcessId);
      CloseHandle(arr[i].procInfo.hProcess);
      CloseHandle(arr[i].procInfo.hThread);
      CloseHandle(arr[i].forwardWrite);
      CloseHandle(arr[i].forwardRead);
      CloseHandle(arr[i].backwardWrite);
      CloseHandle(arr[i].backwardRead);
      TerminateProcess(arr[i].procInfo.hProcess, 0);
    }
  }
  return 0;
}
