#define _CRT_SECURE_NO_WARNINGS // Optional: comment out if using only *_s versions


#include <windows.h>
#include <shellapi.h> // For SHFileOperationW
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
 
#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
 
#include <direct.h>

#include <locale>
#include <codecvt>

#include <SDL2/SDL.h>

//#include "header.h"

#include "Header.h"
#include <stdarg.h>



#define MAX_FILES 256
// Maximum number of drives (26 letters, A-Z)
#define MAX_DRIVES 26

 


 

void logError(const char* format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

#ifdef _MSC_VER
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
#else
    printf("%s\n", buffer);
#endif

    FILE* logFile = nullptr;
#ifdef _MSC_VER
    if (fopen_s(&logFile, "sdl_error.log", "a") == 0 && logFile) {
#else
    logFile = fopen("sdl_error.log", "a");
    if (logFile) {
#endif
        fprintf(logFile, "%s\n", buffer);
        fclose(logFile);
    }
    }

// Convert FILETIME to human-readable string
void filetimeToString(FILETIME ft, wchar_t* buffer, size_t len) {
    SYSTEMTIME stUTC, stLocal;
    FileTimeToSystemTime(&ft, &stUTC);
    SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);
    swprintf(buffer, len, L"%04d-%02d-%02d %02d:%02d:%02d",
        stLocal.wYear, stLocal.wMonth, stLocal.wDay,
        stLocal.wHour, stLocal.wMinute, stLocal.wSecond);
}

 
 

std::string cc(const wchar_t* buf) {
    if (!buf || !*buf) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cc: Null or empty input string");
        return "";
    }

    // Get required buffer size for UTF-8 string
    int utf8_size = WideCharToMultiByte(
        CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr
    );

    if (utf8_size == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cc: WideCharToMultiByte size failed: %ld", GetLastError());
        return "";
    }

    // Allocate buffer for UTF-8 string
    std::string result(utf8_size, '\0');

    // Perform conversion
    int written = WideCharToMultiByte(
        CP_UTF8, 0, buf, -1, &result[0], utf8_size, nullptr, nullptr
    );

    if (written == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cc: WideCharToMultiByte conversion failed: %ld", GetLastError());
        return "";
    }

    // Remove null terminator
    if (!result.empty()) {
        result.resize(result.size() - 1);
    }

    return result;
}



// Scan all valid drive letters on the computer
// Returns the number of drives found, or -1 on error
// Allocates drives array; caller must free each drives[i] and drives
int scanDriveLetters(wchar_t*** drives) {
    if (!drives) {
        logError("Invalid drives pointer");
        return -1;
    }

    // Initialize output array
    *drives = (wchar_t**)malloc(MAX_DRIVES * sizeof(wchar_t*));
    if (!*drives) {
        logError("Failed to allocate drives array");
        return -1;
    }
    for (int i = 0; i < MAX_DRIVES; i++) {
        (*drives)[i] = nullptr;
    }

    // Get all logical drive strings
    wchar_t driveStrings[256] = L"";
    DWORD charsReturned = GetLogicalDriveStringsW(256, driveStrings);
    if (charsReturned == 0 || charsReturned > 256) {
        logError("GetLogicalDriveStringsW failed   ");
        free(*drives);
        *drives = nullptr;
        return -1;
    }

    int driveCount = 0;
    wchar_t* currentDrive = driveStrings;
    while (*currentDrive && driveCount < MAX_DRIVES) {
        // Validate drive type
        UINT driveType = GetDriveTypeW(currentDrive);
        if (driveType == DRIVE_FIXED || driveType == DRIVE_REMOVABLE || driveType == DRIVE_REMOTE) {
            // Allocate and copy drive string (e.g., L"C:\\")
            (*drives)[driveCount] = (wchar_t*)malloc((wcslen(currentDrive) + 1) * sizeof(wchar_t));
            if (!(*drives)[driveCount]) {
                logError("Failed to allocate drive string");
                // Clean up
                for (int i = 0; i < driveCount; i++) {
                    free((*drives)[i]);
                }
                free(*drives);
                *drives = nullptr;
                return -1;
            }
            wcsncpy_s((*drives)[driveCount], wcslen(currentDrive) + 1, currentDrive, _TRUNCATE);
            driveCount++;
        }
        // Move to next drive string (null-terminated)
        currentDrive += wcslen(currentDrive) + 1;
    }

    if (driveCount == 0) {
        logError("No valid drives found");
        free(*drives);
        *drives = nullptr;
        return 0;
    }

    logError("Scan drives");
    return driveCount;
}

// Free the drives array returned by scanDriveLetters
void freeDriveLetters(wchar_t** drives, int driveCount) {
    if (drives) {
        for (int i = 0; i < driveCount; i++) {
            free(drives[i]);
        }
        free(drives);
    }
}


int changeDrive(const char* drive) {
    char path[8];
    snprintf(path, sizeof(path), "%s\\", drive);  // e.g., "C:\\"
    if (_chdir(path) == 0) {
        logError("Changed drive to %s", drive);
    }
    else {
        logError("Failed to change drive to %s", drive);
        return 0;
    }

    return 1;
}



// Extract file extension
const wchar_t* getFileExtensionW(const wchar_t* filename) {
    const wchar_t* dot = wcsrchr(filename, L'.');
    return (dot && dot != filename) ? dot + 1 : L"";
}

// Get all files in the directory
// Get all files in the directory, sorted with drives above folders above files
int getFilesInExecutableDirectory(FileInfo files[], const wchar_t* directory) {
    WIN32_FIND_DATAW findData;
    wchar_t searchPath[MAX_PATH];
    int count = 0;

    if (!files || !directory) {
        logError("Invalid files array or directory.");
        return -1;
    }

    // Construct search path
    swprintf(searchPath, MAX_PATH, L"%s\\*", directory);

    // Find files
    HANDLE hFind = FindFirstFileW(searchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        logError("Failed to open directory: %s", cc(directory));
        return -1;
    }

    do {
        if (count >= MAX_FILES) {
            logError("Maximum file limit reached %d ");
        }

        // Initialize FileInfo entry
        FileInfo* fi = &files[count];
        fi->filename[0] = L'\0';                   // Empty filename
        fi->extension[0] = L'\0';                  // Empty extension
        fi->size.QuadPart = 0;                     // Zero size
        fi->lastModified.dwLowDateTime = 0;        // Zero timestamp
        fi->lastModified.dwHighDateTime = 0;
        fi->attributes = 0;                        // Zero attributes

        // Check if it's a directory and not "." or ".."
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY &&
            wcscmp(findData.cFileName, L".") != 0 &&
            wcscmp(findData.cFileName, L"..") != 0) {
            // Add <foldername> format
            wchar_t tempName[MAX_PATH];
            swprintf(tempName, MAX_PATH, L"<%s>", findData.cFileName);
            wcsncpy_s(fi->filename, MAX_PATH, tempName, _TRUNCATE);
        }
        else {
            // Copy filename as is
            wcsncpy_s(fi->filename, MAX_PATH, findData.cFileName, _TRUNCATE);
        }

        // Populate other fields
        wcsncpy_s(fi->extension, 16, getFileExtensionW(findData.cFileName), _TRUNCATE);
        fi->size.LowPart = findData.nFileSizeLow;
        fi->size.HighPart = findData.nFileSizeHigh;
        fi->lastModified = findData.ftLastWriteTime;
        fi->attributes = findData.dwFileAttributes;

        count++;
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

    /*
    // Append drive letters as [C:], [D:], etc.
    wchar_t** drives = nullptr;
    int driveCount = scanDriveLetters(&drives);
    if (driveCount > 0) {
        for (int i = 0; i < driveCount && count < MAX_FILES; i++) {
            // Initialize FileInfo entry for drive
            FileInfo* fi = &files[count];
            fi->filename[0] = L'\0';
            fi->extension[0] = L'\0';
            fi->size.QuadPart = 0;
            fi->lastModified.dwLowDateTime = 0;
            fi->lastModified.dwHighDateTime = 0;
            fi->attributes = FILE_ATTRIBUTE_DIRECTORY; // Treat drives as directories

            // Format drive as [C:]
            wchar_t driveName[8];
            swprintf(driveName, 8, L"[%c:]", drives[i][0]);
            wcsncpy_s(fi->filename, MAX_PATH, driveName, _TRUNCATE);

            logError("Added drive %  %s",    cc(fi->filename));
            count++;
        }
        // Free drives array
        freeDriveLetters(drives, driveCount);
    }
    else if (driveCount < 0) {
        logError("Failed to scan drives");
    }
    */
    // Sort files: drives above folders above files
    if (count > 0) {
        std::sort(files, files + count, [](const FileInfo& a, const FileInfo& b) {
            // Check if a or b is a drive ([C:] format)
            bool aIsDrive = wcslen(a.filename) == 4 && a.filename[0] == L'[' && a.filename[3] == L']';
            bool bIsDrive = wcslen(b.filename) == 4 && b.filename[0] == L'[' && b.filename[3] == L']';

            // Check if a or b is a folder (has FILE_ATTRIBUTE_DIRECTORY)
            bool aIsDir = (a.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            bool bIsDir = (b.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

            // Drives come first
            if (aIsDrive && !bIsDrive) return true;
            if (!aIsDrive && bIsDrive) return false;

            // If neither or both are drives, sort folders above files
            if (aIsDir && !bIsDir) return true;
            if (!aIsDir && bIsDir) return false;

            // Within same type (folders or files), maintain original order
            return false;
            });
    }

    logError("Sorted   entries: drives, folders, files" );
    return count;
}

// Convert attributes to readable string
std::wstring attributesToString(DWORD attr) {
    std::wstring result;
    if (attr & FILE_ATTRIBUTE_READONLY) result += L"R ";
    if (attr & FILE_ATTRIBUTE_HIDDEN) result += L"H ";
    if (attr & FILE_ATTRIBUTE_SYSTEM) result += L"S ";
    if (attr & FILE_ATTRIBUTE_ARCHIVE) result += L"A ";
    if (attr & FILE_ATTRIBUTE_TEMPORARY) result += L"T ";
    if (attr & FILE_ATTRIBUTE_COMPRESSED) result += L"C ";
    return result.empty() ? L"None" : result;
}

 

// Rename file: infile → newname
int ren(const wchar_t* infile, const wchar_t* newname) {
    if (MoveFileW(infile, newname)) {
        logError("Renamed ok");
        return 0;
    }
    else {
        DWORD err = GetLastError();
        logError("Failed to rename ");
        return -1;
    }
}

// Delete a file and move it to Recycle Bin
int del(const wchar_t* filename) {
    // Double null-terminated string required
    wchar_t from[MAX_PATH + 2] = { 0 };
    wcsncpy_s(from, MAX_PATH + 1, filename, _TRUNCATE);

    SHFILEOPSTRUCTW fileOp = { 0 };
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = from;
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;

    int result = SHFileOperationW(&fileOp);
    if (result == 0 && !fileOp.fAnyOperationsAborted) {
        logError("File moved to Recycle");
        return 0;
    }
    else {
        logError("Failed to delete file");
        return -1;
    }
}




bool goto_folder(wchar_t* path) {
    if (SetCurrentDirectoryW(path)) {
        return true;
    }
    DWORD error = GetLastError();
    logError("SetCurrentDirectoryW failed with error %d for path  ",  cc(path));
    return false;

}



 