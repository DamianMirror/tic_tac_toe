// human.cpp
#include <iostream>
#include <string>
#include <windows.h>
#include <vector>
#include <cstdlib> // For atoi()

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        std::wcerr << L"Usage: human.exe <pipe_name>" << std::endl;
        return 1;
    }

    std::wstring pipeName = argv[1];

    // Attempt to connect to the named pipe
    HANDLE hPipe = NULL;
    while (true) {
        hPipe = CreateFileW(
            pipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, // No sharing
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );

        if (hPipe != INVALID_HANDLE_VALUE)
            break;

        if (GetLastError() != ERROR_PIPE_BUSY) {
            std::wcerr << L"Could not open pipe. GLE=" << GetLastError() << std::endl;
            return 1;
        }

        // All pipe instances are busy, wait
        if (!WaitNamedPipeW(pipeName.c_str(), 5000)) { // Wait up to 5 seconds
            std::wcerr << L"Could not open pipe: 5-second wait timed out." << std::endl;
            return 1;
        }
    }

    std::wcout << L"Connected to server." << std::endl;

    // Set the pipe to message-read mode
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(
        hPipe,
        &mode,
        NULL,
        NULL
    )) {
        std::wcerr << L"SetNamedPipeHandleState failed. GLE=" << GetLastError() << std::endl;
        CloseHandle(hPipe);
        return 1;
    }

    while (true) {
        // Read board state from server
        wchar_t buffer[256];
        DWORD bytesRead;
        BOOL readSuccess = ReadFile(
            hPipe,
            buffer,
            sizeof(buffer) - sizeof(wchar_t),
            &bytesRead,
            NULL
        );

        if (!readSuccess || bytesRead == 0) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                std::wcout << L"Server disconnected." << std::endl;
            }
            else {
                std::wcerr << L"ReadFile failed. GLE=" << GetLastError() << std::endl;
            }
            break;
        }

        buffer[bytesRead / sizeof(wchar_t)] = L'\0';
        std::wstring boardState(buffer);

        std::wcout << L"Received board state: " << boardState << std::endl;

        // Display board state (optional)
        // You can parse and display the board more nicely if desired

        // Prompt user for move
        int move = -1;
        std::wcout << L"Enter your move (0-8): ";
        std::wcin >> move;

        // Validate move
        if (move < 0 || move > 8) {
            std::wcerr << L"Invalid move input: " << move << std::endl;
            move = -1; // Indicate invalid move
        }

        // Convert move to string
        std::wstring moveStr = std::to_wstring(move) + L"\n";

        // Write move back to server
        DWORD bytesWritten;
        if (!WriteFile(
            hPipe,
            moveStr.c_str(),
            static_cast<DWORD>(moveStr.size() * sizeof(wchar_t)),
            &bytesWritten,
            NULL
        )) {
            std::wcerr << L"Failed to write to pipe. GLE=" << GetLastError() << std::endl;
            break;
        }

        std::wcout << L"Sent move: " << move << std::endl;
    }

    CloseHandle(hPipe);
    return 0;
}
