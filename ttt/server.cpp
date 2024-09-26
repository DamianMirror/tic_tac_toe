// server.cpp
#include <iostream>
#include <vector>       // For std::vector
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sstream>      // For std::wstringstream
#include <string>

// Structure to hold client process information
struct ClientProcess {
    std::wstring pipeName; // Name of the named pipe
    HANDLE hPipe;          // Handle to the named pipe
    HANDLE hProcess;       // Handle to the client process
};

// TicTacToeBoard Class Definition
class TicTacToeBoard {
public:
    TicTacToeBoard() {
        reset();
    }

    void reset() {
        board = std::vector<char>(9, ' ');
    }

    bool makeMove(int pos, char player) {
        if (pos >= 0 && pos < 9 && board[pos] == ' ') {
            board[pos] = player;
            return true;
        }
        return false;
    }

    char& operator[](int pos) {
        return board[pos];
    }

    void display() const {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        for (int i = 0; i < 9; i++) {
            if (board[i] == ' ') {
                std::wcout << i;  // Print index for empty cells
            }
            else {
                // Change color to red for X or O
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                std::wcout << board[i];  // Print 'X' or 'O'
                SetConsoleTextAttribute(hConsole, 7);  // Reset to default color
            }
            if ((i + 1) % 3 == 0) {
                std::wcout << std::endl;
            }
            else {
                std::wcout << L" | ";
            }
        }
        std::wcout << std::endl;
    }

    char checkWinner() const {
        static const int winPatterns[8][3] = {
            {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
            {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
            {0, 4, 8}, {2, 4, 6}
        };

        for (auto& pattern : winPatterns) {
            if (board[pattern[0]] == board[pattern[1]] &&
                board[pattern[1]] == board[pattern[2]] &&
                board[pattern[0]] != ' ') {
                return board[pattern[0]];
            }
        }
        return ' ';
    }

    bool isFull() const {
        for (char cell : board) {
            if (cell == ' ') return false;
        }
        return true;
    }

private:
    std::vector<char> board;
};

// Function to create a named pipe, launch client process, and wait for connection
bool createClientProcess(const std::wstring& pipeName, const std::wstring& exePath, ClientProcess& client) {
    client.pipeName = pipeName;

    // Create a named pipe
    client.hPipe = CreateNamedPipeW(
        pipeName.c_str(),
        PIPE_ACCESS_DUPLEX,                      // Read/Write access
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, // Message-type pipe
        1,                                       // Max instances
        512,                                     // Out buffer size
        512,                                     // In buffer size
        0,                                       // Default timeout
        NULL                                     // Default security attributes
    );

    if (client.hPipe == INVALID_HANDLE_VALUE) {
        std::wcerr << L"Failed to create named pipe: " << pipeName << L". GLE=" << GetLastError() << std::endl;
        return false;
    }

    // Launch the client process, passing the pipe name as an argument
    STARTUPINFOW si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW; // Show the client console window

    // Prepare the command line: "bot1.exe \\.\pipe\TicTacToeBot1"
    std::wstring commandLine = exePath + L" " + pipeName;

    // Create the client process
    if (!CreateProcessW(
        NULL,                   // No module name (use command line)
        &commandLine[0],        // Command line
        NULL,                   // Process handle not inheritable
        NULL,                   // Thread handle not inheritable
        FALSE,                  // Set handle inheritance to FALSE
        CREATE_NEW_CONSOLE,     // Create a new console window
        NULL,                   // Use parent's environment block
        NULL,                   // Use parent's starting directory
        &si,                    // Pointer to STARTUPINFO structure
        &pi                     // Pointer to PROCESS_INFORMATION structure
    )) {
        std::wcerr << L"Failed to launch client process: " << exePath << L". GLE=" << GetLastError() << std::endl;
        CloseHandle(client.hPipe);
        return false;
    }

    client.hProcess = pi.hProcess;
    CloseHandle(pi.hThread); // We don't need the thread handle

    std::wcout << L"Launched client process: " << exePath << L" with pipe: " << pipeName << std::endl;

    // Wait for the client to connect to the pipe
    BOOL connected = ConnectNamedPipe(client.hPipe, NULL) ?
        TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

    if (!connected) {
        std::wcerr << L"Failed to connect to client on pipe: " << pipeName << L". GLE=" << GetLastError() << std::endl;
        CloseHandle(client.hPipe);
        CloseHandle(client.hProcess);
        return false;
    }

    std::wcout << L"Client connected on pipe: " << pipeName << std::endl;
    return true;
}

// Function to send the board state and receive a move from a client
int getMove(HANDLE hPipe, TicTacToeBoard& board) {
    // Prepare the board state as a string
    std::wstringstream ss;
    for (int i = 0; i < 9; ++i) {
        ss << board[i];
    }
    ss << std::endl;  // Ensure there's a newline character
    std::wstring boardState = ss.str();

    // Write the board state to the process's pipe
    DWORD bytesWritten;
    if (!WriteFile(hPipe, boardState.c_str(), static_cast<DWORD>(boardState.size() * sizeof(wchar_t)), &bytesWritten, NULL)) {
        std::wcerr << L"Failed to write to process pipe. GLE=" << GetLastError() << std::endl;
        return -1;
    }

    // Read the move from the process's pipe
    wchar_t moveBuffer[256];
    DWORD bytesRead;
    if (!ReadFile(hPipe, moveBuffer, sizeof(moveBuffer) - sizeof(wchar_t), &bytesRead, NULL) || bytesRead == 0) {
        std::wcerr << L"Failed to read from process pipe. GLE=" << GetLastError() << std::endl;
        return -1;
    }
    moveBuffer[bytesRead / sizeof(wchar_t)] = L'\0';

    int move = _wtoi(moveBuffer);
    return move;
}

// Function to play the TicTacToe game based on the selected mode
void playGame(int mode) {
    TicTacToeBoard board;
    int moveCount = 0;
    char currentPlayer = 'X';

    // Structures to hold client information
    ClientProcess human1Client; // Player 1
    ClientProcess human2Client; // Player 2
    ClientProcess bot1Client;   // Bot1
    ClientProcess bot2Client;   // Bot2 (only in Bot vs Bot mode)

    // Define pipe names
    std::wstring pipeNameHuman1 = L"\\\\.\\pipe\\TicTacToeHuman1";
    std::wstring pipeNameHuman2 = L"\\\\.\\pipe\\TicTacToeHuman2";
    std::wstring pipeNameBot1 = L"\\\\.\\pipe\\TicTacToeBot1";
    std::wstring pipeNameBot2 = L"\\\\.\\pipe\\TicTacToeBot2";

    // Paths to client executables
    std::wstring humanExePath = L"human.exe"; // Ensure human.exe exists in the same directory
    std::wstring bot1ExePath = L"bot1.exe";  // Ensure bot1.exe exists in the same directory
    std::wstring bot2ExePath = L"bot2.exe";  // Ensure bot2.exe exists in the same directory

    // Initialize clients based on game mode
    if (mode == 1) { // Human vs Human
        std::wcout << L"Human vs Human mode selected. Launching two human processes." << std::endl;
        // Create and launch human1 process
        if (!createClientProcess(pipeNameHuman1, humanExePath, human1Client)) {
            std::wcerr << L"Failed to set up Human1." << std::endl;
            return;
        }
        // Create and launch human2 process
        if (!createClientProcess(pipeNameHuman2, humanExePath, human2Client)) {
            std::wcerr << L"Failed to set up Human2." << std::endl;
            return;
        }
    }
    else if (mode == 2) { // Human vs Bot
        std::wcout << L"Human vs Bot mode selected. Launching one human and one bot process." << std::endl;
        // Create and launch human1 process
        if (!createClientProcess(pipeNameHuman1, humanExePath, human1Client)) {
            std::wcerr << L"Failed to set up Human1." << std::endl;
            return;
        }
        // Create and launch bot1 process
        if (!createClientProcess(pipeNameBot1, bot1ExePath, bot1Client)) {
            std::wcerr << L"Failed to set up Bot1." << std::endl;
            return;
        }
    }
    else if (mode == 3) { // Bot vs Bot
        std::wcout << L"Bot vs Bot mode selected. Launching two bot processes." << std::endl;
        // Create and launch bot1 process
        if (!createClientProcess(pipeNameBot1, bot1ExePath, bot1Client)) {
            std::wcerr << L"Failed to set up Bot1." << std::endl;
            return;
        }
        // Create and launch bot2 process
        if (!createClientProcess(pipeNameBot2, bot2ExePath, bot2Client)) {
            std::wcerr << L"Failed to set up Bot2." << std::endl;
            return;
        }
    }

    // Game loop
    while (true) {
        board.display();
        int pos = -1;

        if (mode == 1) { // Human vs Human
            if (currentPlayer == 'X') {
                pos = getMove(human1Client.hPipe, board);
                if (pos == -1) {
                    std::wcerr << L"Human1 failed to provide a move." << std::endl;
                    break;
                }
                std::wcout << L"Human1 (X) chose move: " << pos << std::endl;
            }
            else { // Player O
                pos = getMove(human2Client.hPipe, board);
                if (pos == -1) {
                    std::wcerr << L"Human2 failed to provide a move." << std::endl;
                    break;
                }
                std::wcout << L"Human2 (O) chose move: " << pos << std::endl;
            }
        }
        else if (mode == 2) { // Human vs Bot
            if (currentPlayer == 'X') { // Human's turn
                pos = getMove(human1Client.hPipe, board);
                if (pos == -1) {
                    std::wcerr << L"Human1 failed to provide a move." << std::endl;
                    break;
                }
                std::wcout << L"Human1 (X) chose move: " << pos << std::endl;
            }
            else { // Bot's turn
                pos = getMove(bot1Client.hPipe, board);
                if (pos == -1) {
                    std::wcerr << L"Bot1 failed to provide a move." << std::endl;
                    break;
                }
                std::wcout << L"Bot1 (O) chose move: " << pos << std::endl;
            }
        }
        else if (mode == 3) { // Bot vs Bot
            if (currentPlayer == 'X') { // Bot1's turn
                pos = getMove(bot1Client.hPipe, board);
                if (pos == -1) {
                    std::wcerr << L"Bot1 failed to provide a move." << std::endl;
                    break;
                }
                std::wcout << L"Bot1 (X) chose move: " << pos << std::endl;
            }
            else { // Bot2's turn
                pos = getMove(bot2Client.hPipe, board);
                if (pos == -1) {
                    std::wcerr << L"Bot2 failed to provide a move." << std::endl;
                    break;
                }
                std::wcout << L"Bot2 (O) chose move: " << pos << std::endl;
            }
        }

        // Validate the move
        if (pos < 0 || pos > 8) {
            std::wcerr << L"Invalid move input: " << pos << std::endl;
            if (mode == 1 || mode == 2 || mode == 3) {
                continue; // Skip invalid move
            }
        }

        // Attempt to make the move
        if (!board.makeMove(pos, currentPlayer)) {
            std::wcerr << L"Invalid move. Cell already occupied or out of range." << std::endl;
            if (mode == 1 || mode == 2 || mode == 3) {
                continue; // Skip invalid move
            }
        }

        moveCount++;

        // Check for a winner or draw
        char winner = board.checkWinner();
        if (winner != ' ' || board.isFull()) {
            board.display();
            if (winner != ' ') {
                std::wcout << L"Winner: " << winner << std::endl;
            }
            else {
                std::wcout << L"It's a draw!" << std::endl;
            }
            break;
        }

        // Toggle player
        currentPlayer = (currentPlayer == 'X') ? 'O' : 'X';
    }

    // Terminate and clean up client processes after the game ends
    if (mode == 1) { // Human vs Human
        if (human1Client.hProcess != NULL) {
            TerminateProcess(human1Client.hProcess, 0);
            CloseHandle(human1Client.hProcess);
            CloseHandle(human1Client.hPipe);
        }
        if (human2Client.hProcess != NULL) {
            TerminateProcess(human2Client.hProcess, 0);
            CloseHandle(human2Client.hProcess);
            CloseHandle(human2Client.hPipe);
        }
    }
    else if (mode == 2) { // Human vs Bot
        if (human1Client.hProcess != NULL) {
            TerminateProcess(human1Client.hProcess, 0);
            CloseHandle(human1Client.hProcess);
            CloseHandle(human1Client.hPipe);
        }
        if (bot1Client.hProcess != NULL) {
            TerminateProcess(bot1Client.hProcess, 0);
            CloseHandle(bot1Client.hProcess);
            CloseHandle(bot1Client.hPipe);
        }
    }
    else if (mode == 3) { // Bot vs Bot
        if (bot1Client.hProcess != NULL) {
            TerminateProcess(bot1Client.hProcess, 0);
            CloseHandle(bot1Client.hProcess);
            CloseHandle(bot1Client.hPipe);
        }
        if (bot2Client.hProcess != NULL) {
            TerminateProcess(bot2Client.hProcess, 0);
            CloseHandle(bot2Client.hProcess);
            CloseHandle(bot2Client.hPipe);
        }
    }

    // Wait for user input before exiting
    std::wcout << L"Press Enter to exit...";
    std::wcin.get();
}

// Main Function
int wmain() {
    // Set the console to handle Unicode output
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    int mode;
    std::wcout << L"Select game mode:\n";
    std::wcout << L"1. Human vs Human\n";
    std::wcout << L"2. Human vs Bot\n";
    std::wcout << L"3. Bot vs Bot\n";
    std::wcout << L"Enter your choice: ";
    std::wcin >> mode;

    if (mode < 1 || mode > 3) {
        std::wcerr << L"Invalid game mode." << std::endl;
        return 1;
    }

    playGame(mode);
    return 0;
}
