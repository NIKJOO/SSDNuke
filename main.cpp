#include <iostream>
#include <windows.h>
#include <random>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>
#include <functional>


// Function prototypes
void listDrives();
bool FillRandom(BYTE* buffer, DWORD size, std::minstd_rand& rng);
bool FillRandom(BYTE* buffer, DWORD size, std::mt19937& rng);
bool IsValidDrive(wchar_t driveLetter);
void wipeEmptySpace(wchar_t driveLetter);
bool WipeMFT(const std::wstring& volumePath);
bool getVolumeSize(wchar_t driveLetter, ULARGE_INTEGER& totalSize, ULARGE_INTEGER& freeSize);
void displayProgressBar(unsigned long long completed, unsigned long long total, const std::string& operation);
bool SendTrim(wchar_t driveLetter);
bool IsNTFS(wchar_t driveLetter);

std::mutex mtx;
std::atomic<bool> diskFull(false);

std::wstring generateRandomString(size_t length, std::minstd_rand& rng) {
    const wchar_t charset[] = L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::wstring result;
    std::uniform_int_distribution<> dist(0, sizeof(charset) / sizeof(wchar_t) - 2);
    for (size_t i = 0; i < length; ++i) {
        result += charset[dist(rng)];
    }
    return result;
}

bool getVolumeSize(wchar_t driveLetter, ULARGE_INTEGER& totalSize, ULARGE_INTEGER& freeSize) {
    std::wstring volumePath = std::wstring(1, driveLetter) + L":\\";
    if (!GetDiskFreeSpaceExW(volumePath.c_str(), &freeSize, &totalSize, NULL)) {
        std::wcerr << L"[-] Error getting volume size for " << volumePath << L": " << GetLastError() << std::endl;
        return false;
    }
    return true;
}

void displayProgressBar(unsigned long long completed, unsigned long long total, const std::string& operation) {
    std::lock_guard<std::mutex> lock(mtx);
    const int barWidth = 70;
    float progress = total > 0 ? std::min(static_cast<float>(completed) / total, 1.0f) : 0.0f;
    std::cout << "\r[" << operation << "] [";
    int pos = static_cast<int>(barWidth * progress);
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << static_cast<int>(progress * 100.0) << " %";
    std::cout.flush();
}

bool FillRandom(BYTE* buffer, DWORD size, std::minstd_rand& rng) {
    std::uniform_int_distribution<BYTE> dist(0, 255);
    std::generate(buffer, buffer + size, [&]() { return dist(rng); });
    return true;
}

bool FillRandom(BYTE* buffer, DWORD size, std::mt19937& rng) {
    std::uniform_int_distribution<> dist(0, 255);
    for (DWORD i = 0; i < size; ++i) {
        buffer[i] = static_cast<BYTE>(dist(rng));
    }
    return true;
}

bool IsNTFS(wchar_t driveLetter) {
    std::wstring volumePath = std::wstring(1, driveLetter) + L":\\";
    WCHAR fileSystemName[MAX_PATH + 1];
    if (!GetVolumeInformationW(volumePath.c_str(), NULL, 0, NULL, NULL, NULL, fileSystemName, MAX_PATH + 1)) {
        std::wcerr << L"[-] Failed to get filesystem type for " << volumePath << L": " << GetLastError() << L"\n";
        return false;
    }
    return wcscmp(fileSystemName, L"NTFS") == 0;
}

void wipeEmptySpace(wchar_t driveLetter) {
    ULARGE_INTEGER totalSize, freeSize;
    if (!getVolumeSize(driveLetter, totalSize, freeSize)) {
        return;
    }

    unsigned long long totalFreeSpace = freeSize.QuadPart;
    std::atomic<unsigned long long> completedTasks(0);
    std::random_device rd;
    std::minstd_rand rng(rd());
    std::uniform_int_distribution<> filesPerFolderDist(5, 15); 

    const size_t fileSize = 1024 * 1024; 
    size_t maxFiles = std::min(static_cast<size_t>(totalFreeSpace / fileSize), static_cast<size_t>(65535));
    std::vector<std::wstring> createdFiles;
    std::vector<std::wstring> createdFolders;
    createdFiles.reserve(maxFiles);
    createdFolders.reserve(maxFiles / 10); 

    std::wcout << L"[*] Creating and wiping " << maxFiles << L" 1 MB files across multiple folders on drive " << driveLetter << L"\n";

    const size_t bufferSize = 256 * 1024; 
    std::vector<BYTE> buffer(bufferSize);
    bool writeFailed = false;
    size_t filesCreated = 0;

    while (filesCreated < maxFiles && !diskFull && !writeFailed) {
        std::wstring folderName = std::wstring(1, driveLetter) + L":\\" + generateRandomString(150, rng);
        if (!CreateDirectoryW(folderName.c_str(), NULL)) {
            std::wcerr << L"[-] Failed to create folder: " << folderName << L", Error: " << GetLastError() << L"\n";
            writeFailed = true;
            break;
        }
        createdFolders.push_back(folderName);
        size_t filesInThisFolder = std::min(static_cast<size_t>(filesPerFolderDist(rng)), maxFiles - filesCreated);

        for (size_t i = 0; i < filesInThisFolder && !diskFull && !writeFailed; ++i) {
            ULARGE_INTEGER currentFreeSize;
            if (!getVolumeSize(driveLetter, totalSize, currentFreeSize)) {
                std::wcerr << L"[-] Failed to get volume size during wipe: Error " << GetLastError() << L"\n";
                diskFull = true;
                break;
            }

            if (currentFreeSize.QuadPart < fileSize) {
                std::lock_guard<std::mutex> lock(mtx);
                diskFull = true;
                totalFreeSpace = completedTasks;
                break;
            }

            std::wstring fileName = folderName + L"\\" + generateRandomString(25, rng) + L"." + generateRandomString(15, rng) ;
            HANDLE hFile = CreateFileW(fileName.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE) {
                std::wcerr << L"[-] Failed to create file: " << fileName << L", Error: " << GetLastError() << L"\n";
                writeFailed = true;
                break;
            }

            size_t bytesWritten = 0;
            bool fileSuccess = true;
            for (size_t j = 0; j < fileSize / bufferSize && fileSuccess; ++j) {
                FillRandom(buffer.data(), bufferSize, rng);
                DWORD bytesTransferred;
                if (!WriteFile(hFile, buffer.data(), bufferSize, &bytesTransferred, NULL) || bytesTransferred != bufferSize) {
                    std::wcerr << L"[-] Failed to write to file: " << fileName << L", Error: " << GetLastError() << L"\n";
                    fileSuccess = false;
                    writeFailed = true;
                    break;
                }
                bytesWritten += bytesTransferred;
            }

            CloseHandle(hFile);
            if (fileSuccess) {
                createdFiles.push_back(fileName);
                completedTasks += bytesWritten;
                filesCreated++;
            } else {
                DeleteFileW(fileName.c_str());
                break;
            }
            displayProgressBar(completedTasks, totalFreeSpace, "Wipe Empty Space");
        }
    }
    size_t deletedFiles = 0;
    for (const auto& file : createdFiles) {
        SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileW(file.c_str())) {
            deletedFiles++;
        } else {
            std::wcerr << L"[-] Failed to delete file: " << file << L", Error: " << GetLastError() << L"\n";
        }
    }
    size_t deletedFolders = 0;
    for (const auto& folder : createdFolders) {
        SetFileAttributesW(folder.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (RemoveDirectoryW(folder.c_str())) {
            deletedFolders++;
        } else {
            std::wcerr << L"[-] Failed to remove folder: " << folder << L", Error: " << GetLastError() << L"\n";
        }
    }
    if (diskFull || writeFailed || completedTasks >= totalFreeSpace) {
        completedTasks = totalFreeSpace;
    }
    displayProgressBar(completedTasks, totalFreeSpace, "Wipe Empty Space");
    std::cout << std::endl;
    if (diskFull || writeFailed) {
        std::cout << "[!] Wiping stopped: Disk is full or an error occurred.\n";
    } else {
        std::wcout << L"[+] Wiped empty space on drive " << driveLetter << L" with " << deletedFiles << L" 1 MB files across " << deletedFolders << L" folders\n";
    }
    std::cout << "[+] Deleted " << deletedFiles << " of " << createdFiles.size() << " files and " << deletedFolders << " of " << createdFolders.size() << " folders.\n";
}

void wipeExistingFiles(wchar_t driveLetter) {
    std::random_device rd;
    std::minstd_rand rng(rd());
    std::mutex fileMutex;
    std::queue<std::wstring> fileQueue;
    std::atomic<unsigned long long> processedFiles(0);
    unsigned long long totalFiles = 0;


    std::function<void(const std::wstring&)> collectFiles = [&](const std::wstring& currentPath) {
        std::wstring searchPath = currentPath + (currentPath.back() == L'\\' ? L"" : L"\\") + L"*.*";
        WIN32_FIND_DATAW findFileData;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findFileData);
        if (hFind == INVALID_HANDLE_VALUE) return;

        std::vector<std::wstring> subDirs;
        do {
            const wchar_t* name = findFileData.cFileName;
            if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) continue;
            std::wstring fullPath = currentPath + (currentPath.back() == L'\\' ? L"" : L"\\") + name;
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (std::wstring(name) != L"$RECYCLE.BIN" && std::wstring(name) != L"System Volume Information") {
                    subDirs.push_back(fullPath);
                }
            } else {
                std::lock_guard<std::mutex> lock(fileMutex);
                fileQueue.push(fullPath);
                totalFiles++;
            }
        } while (FindNextFileW(hFind, &findFileData));
        FindClose(hFind);

        for (const auto& dir : subDirs) {
            collectFiles(dir);
        }
    };

    std::wstring rootPath = std::wstring(1, driveLetter) + L":\\";
    collectFiles(rootPath);

    if (totalFiles == 0) {
        std::cout << "[*] No accessible files found to wipe on drive " << (char)driveLetter << ":\n";
        return;
    }

    std::atomic<unsigned long long> completedTasks(0);
    unsigned long long totalTasks = totalFiles * 5; 

    std::cout << "[*] File Wipe Operation Started (" << totalFiles << " files to process).\n";

    auto wipeFile = [&](const std::wstring& fullPath, std::minstd_rand& localRng) {
        SetFileAttributesW(fullPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            std::wcerr << L"[-] Failed to open file: " << fullPath << L", Error: " << GetLastError() << L"\n";
            return;
        }

        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(hFile, &fileSize)) {
            std::wcerr << L"[-] Failed to get size of file: " << fullPath << L", Error: " << GetLastError() << L"\n";
            CloseHandle(hFile);
            return;
        }

        std::vector<BYTE> buffer(256 * 1024); 
        bool wipeSuccess = true;

        for (int pass = 0; pass < 3 && wipeSuccess; ++pass) {
            LARGE_INTEGER li = {0};
            if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN)) {
                std::wcerr << L"[-] Failed to set file pointer for: " << fullPath << L", Error: " << GetLastError() << L"\n";
                wipeSuccess = false;
                break;
            }

            for (LONGLONG offset = 0; offset < fileSize.QuadPart; offset += buffer.size()) {
                DWORD bytesToWrite = static_cast<DWORD>(std::min(static_cast<LONGLONG>(buffer.size()), fileSize.QuadPart - offset));
                if (pass == 0 || pass == 2) {
                    FillRandom(buffer.data(), bytesToWrite, localRng);
                } else {
                    std::fill(buffer.begin(), buffer.begin() + bytesToWrite, 0xFF); // Use all 1s for pass 2
                }

                DWORD bytesWritten;
                if (!WriteFile(hFile, buffer.data(), bytesToWrite, &bytesWritten, NULL) || bytesWritten != bytesToWrite) {
                    std::wcerr << L"[-] Failed to write pass " << pass + 1 << L" for: " << fullPath << L", Error: " << GetLastError() << L"\n";
                    wipeSuccess = false;
                    break;
                }
            }

            if (!FlushFileBuffers(hFile)) {
                std::wcerr << L"[-] Failed to flush file: " << fullPath << L", Error: " << GetLastError() << L"\n";
                wipeSuccess = false;
            }

            if (wipeSuccess) {
                completedTasks++;
            }
        }

        CloseHandle(hFile);

        if (wipeSuccess) {
            std::wstring tempName = fullPath.substr(0, fullPath.find_last_of(L'\\') + 1) + generateRandomString(10, localRng) + L"." + generateRandomString(25, localRng);
            if (MoveFileW(fullPath.c_str(), tempName.c_str())) {
                completedTasks++;
                if (DeleteFileW(tempName.c_str())) {
                    completedTasks++;
                } else {
                    std::wcerr << L"[-] Failed to delete file: " << tempName << L", Error: " << GetLastError() << L"\n";
                }
            } else {
                std::wcerr << L"[-] Failed to rename file: " << fullPath << L", Error: " << GetLastError() << L"\n";
            }
        }

        processedFiles++;
        if (processedFiles % 10 == 0) { 
            displayProgressBar(completedTasks, totalTasks, "Wipe Files");
        }
    };

    const size_t numThreads = std::min(std::thread::hardware_concurrency(), 8u);
    std::vector<std::thread> threads;
    std::random_device rdThread;
    for (size_t i = 0; i < numThreads; ++i) {
        threads.emplace_back([&]() {
            std::minstd_rand localRng(rdThread());
            while (true) {
                std::wstring filePath;
                {
                    std::lock_guard<std::mutex> lock(fileMutex);
                    if (fileQueue.empty()) break;
                    filePath = fileQueue.front();
                    fileQueue.pop();
                }
                wipeFile(filePath, localRng);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::function<void(const std::wstring&)> removeDirs = [&](const std::wstring& currentPath) {
        std::wstring searchPath = currentPath + (currentPath.back() == L'\\' ? L"" : L"\\") + L"*.*";
        WIN32_FIND_DATAW findFileData;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findFileData);
        if (hFind == INVALID_HANDLE_VALUE) return;

        std::vector<std::wstring> subDirs;
        do {
            const wchar_t* name = findFileData.cFileName;
            if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) continue;
            std::wstring fullPath = currentPath + (currentPath.back() == L'\\' ? L"" : L"\\") + name;
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (std::wstring(name) != L"$RECYCLE.BIN" && std::wstring(name) != L"System Volume Information") {
                    subDirs.push_back(fullPath);
                }
            }
        } while (FindNextFileW(hFind, &findFileData));
        FindClose(hFind);

        for (const auto& dir : subDirs) {
            removeDirs(dir);
        }

        if (currentPath == rootPath) {
            return;
        }

        SetFileAttributesW(currentPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!RemoveDirectoryW(currentPath.c_str())) {
            DWORD error = GetLastError();
            if (error != ERROR_DIR_NOT_EMPTY) { 
                std::wcerr << L"[-] Failed to remove directory: " << currentPath << L", Error: " << error << L"\n";
            }
        }
    };

    removeDirs(rootPath);

    displayProgressBar(completedTasks, totalTasks, "Wipe Files");
    std::cout << "\r" << std::string(100, ' ') << "\r"; 
    std::cout << "[+] Completed wiping, renaming, and deleting " << processedFiles << " of " << totalFiles << " accessible files and folders on drive " << (char)driveLetter << ":\n";
}


bool WipeMFT(const std::wstring& volumePath) {} //The contents of this function have been removed to prevent possible misuse.

bool SendTrim(wchar_t driveLetter) {
    std::string command = "defrag ";
    command += (char)driveLetter;
    command += ": /L";  

    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "[-] Failed to run TRIM on drive " << (char)driveLetter << "\n";
        return false;
    }

    char buffer[256];
    std::cout << "[*] TRIM started on " << (char)driveLetter << ": ...\n";
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        std::cout << buffer; 
    }

    int exitCode = _pclose(pipe);
    if (exitCode != 0) {
        std::cerr << "[-] TRIM failed on drive " << (char)driveLetter << " (exit code " << exitCode << ")\n";
        return false;
    }

    std::cout << "[+] TRIM completed successfully on " << (char)driveLetter << ":\n";
    return true;
}

void listDrives() {
    std::cout << "Available drives:" << std::endl;
    for (wchar_t drive = L'A'; drive <= L'Z'; ++drive) {
        std::wstring drivePath = std::wstring(1, drive) + L":\\";
        DWORD dwAttrib = GetFileAttributesW(drivePath.c_str());
        if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wcout << drive << L": " << drivePath << std::endl;
        }
    }
}

bool IsValidDrive(wchar_t driveLetter) {
    std::wstring drivePath = std::wstring(1, towupper(driveLetter)) + L":\\";
    DWORD dwAttrib = GetFileAttributesW(drivePath.c_str());
    return (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

void PrintSSDNUKELogo() {
    std::cout << R"(
 ____  ____  ____    _      _     _  __ _____
/ ___\/ ___\/  _ \  / \  /|/ \ /\/ |/ //  __/
|    \|    \| | \|  | |\ ||| | |||   / |  \  
\___ |\___ || |_/|  | | \||| \_/||   \ |  /_ 
\____/\____/\____/  \_/  \|\____/\_|\_\\____\

BY : Nima Nikjoo
)" << std::endl;
}


int main() {
    PrintSSDNUKELogo();

    std::cout << "\n============================================\n";
    std::cout << "WARNING: Misuse of this application may cause\n";
    std::cout << "         irreversible data loss.\n";
    std::cout << "         Proceed with caution.\n";
    std::cout << "============================================\n\n";

    BOOL isElevated = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD dwSize;
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
            isElevated = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    if (!isElevated) {
        std::cerr << "[-] This program requires administrative privileges. Please run as Administrator.\n";
        return 1;
    }

    int choice = 0;
    std::cout << "Select Wipe Method:\n\n";
    std::cout << "  1. Low Security (MFT Destruction)    \n";
    std::cout << "  2. Medium Security (Wipe all data + MFT Destruction)\n";
    std::cout << "  3. High Security   (Wipe all data + Empty Space + MFT Destruction)\n\n";
    std::cout << "Enter choice (1-3): ";
    std::cin >> choice;

    if (choice < 1 || choice > 3) {
        std::cerr << "[-] Invalid option selected.\n";
        return 1;
    }

    listDrives();
    char driveLetter;
    std::cout << "\nEnter the drive letter (e.g., 'D') for Wipe: ";
    std::cin >> driveLetter;
    wchar_t wDriveLetter = towupper(static_cast<wchar_t>(driveLetter));

    if (!IsValidDrive(wDriveLetter)) {
        std::cerr << "[-] Invalid or inaccessible drive: " << driveLetter << "\n";
        return 1;
    }

    std::cout << "[*] Starting wipe operations on drive " << driveLetter << "...\n";

    if (!SendTrim(wDriveLetter)) {
        std::cerr << "[-] SendTrim failed or not supported. Continuing...\n";
    }

    if (choice >= 2) {
        std::cout << "[*] Wiping existing files...\n";
        wipeExistingFiles(wDriveLetter); 
    }

    if (choice == 3) {
        std::cout << "[*] Wiping empty space...\n";
        wipeEmptySpace(wDriveLetter); 
    }

    std::cout << "[*] Wiping MFT on drive " << driveLetter << "...\n";
    std::wstring volumePath = L"\\\\.\\" + std::wstring(1, wDriveLetter) + L":";
    if (!IsNTFS(wDriveLetter)) {
        std::cerr << "[-] Drive " << driveLetter << " is not NTFS. Skipping MFT wipe.\n";
    } else if (!WipeMFT(volumePath)) {
        std::cerr << "[-] Failed to wipe MFT on drive " << driveLetter << "\n";
    }

    std::cout << "\r" << std::string(100, ' ') << "\r";
    std::cout << "[+] Wipe operation completed.\n";
    return 0;
}
