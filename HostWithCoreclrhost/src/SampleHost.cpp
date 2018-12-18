#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "coreclrhost.h"

#define MANAGED_ASSEMBLY "ManagedLibrary.dll"

#if WINDOWS
#include <Windows.h>
#define FS_SEPERATOR "\\"
#define PATH_DELIMITER ";"
#define CORECLR_FILE_NAME "coreclr.dll"
#elif LINUX
#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>
#define FS_SEPERATOR "/"
#define PATH_DELIMITER ":"
#define MAX_PATH PATH_MAX
#define CORECLR_FILE_NAME "libcoreclr.so"
#endif

// Function pointers type for the managed call and callback
typedef int (*report_callback_ptr)(int progress);
typedef const char* (*doWork_ptr)(const char* jobName, int iterations, int dataSize, double* data, report_callback_ptr callbackFunction);

void BuildTpaList(const char* directory, const char* extension, std::string& tpaList);
int ReportProgressCallback(int progress);

int main(int argc, char* argv[])
{
    // Get the current executable's directory
    // This sample assumes that both CoreCLR and the
    // managed assembly to be loaded are next to this host
    // so we need to get the current path in order to locate those.
    char runtimePath[MAX_PATH];
#if WINDOWS
    GetFullPathNameA(argv[0], MAX_PATH, runtimePath, NULL);
#elif LINUX
    realpath(argv[0], runtimePath);
#endif
    char *last_slash = strrchr(runtimePath, FS_SEPERATOR[0]);
    if (last_slash != NULL)
        *last_slash = 0;

    // Construct the CoreCLR path
    std::string coreClrPath(runtimePath);
    coreClrPath.append(FS_SEPERATOR);
    coreClrPath.append(CORECLR_FILE_NAME);

    // Construct the managed library path
    std::string managedLibraryPath(runtimePath);
    managedLibraryPath.append(FS_SEPERATOR);
    managedLibraryPath.append(MANAGED_ASSEMBLY);
    
    // Load CoreCLR
#if WINDOWS
    HMODULE coreClr = LoadLibraryExA(coreClrPath.c_str(), NULL, 0);
#elif LINUX 
    void *coreClr = dlopen(coreClrPath.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif 
    if (coreClr == NULL)
    {
        printf("ERROR: Failed to load CoreCLR from %s\n", coreClrPath.c_str());
        // HRESULT err = GetLastError();
        return -1;
    }
    else
    {
        printf("Loaded CoreCLR from %s\n", coreClrPath.c_str());
    }

    // Get CoreCLR hosting functions
#if WINDOWS
    coreclr_initialize_ptr initializeCoreClr = (coreclr_initialize_ptr)GetProcAddress(coreClr, "coreclr_initialize");
    coreclr_create_delegate_ptr createManagedDelegate = (coreclr_create_delegate_ptr)GetProcAddress(coreClr, "coreclr_create_delegate");
    coreclr_shutdown_ptr shutdownCoreClr = (coreclr_shutdown_ptr)GetProcAddress(coreClr, "coreclr_shutdown");
#elif LINUX    
    coreclr_initialize_ptr initializeCoreClr = (coreclr_initialize_ptr)dlsym(coreClr, "coreclr_initialize");
    coreclr_create_delegate_ptr createManagedDelegate = (coreclr_create_delegate_ptr)dlsym(coreClr, "coreclr_create_delegate");
    coreclr_shutdown_ptr shutdownCoreClr = (coreclr_shutdown_ptr)dlsym(coreClr, "coreclr_shutdown");
#endif

    if (initializeCoreClr == NULL)
    {
        printf("coreclr_initialize not found");
        return -1;
    }

    if (createManagedDelegate == NULL)
    {
        printf("coreclr_create_delegate not found");
        return -1;
    }

    if (shutdownCoreClr == NULL)
    {
        printf("coreclr_shutdown not found");
        return -1;
    }

    // Construct the trusted platform assemblies (TPA) list
    // This is the list of assemblies that .NET Core can load as
    // trusted system assemblies (similar to the .NET Framework GAC).
    // For this host (as with most), assemblies next to CoreCLR will 
    // be included in the TPA list
    std::string tpaList;
    BuildTpaList(runtimePath, ".dll", tpaList);

    // Define CoreCLR properties
    const char* propertyKeys[] = {
        "TRUSTED_PLATFORM_ASSEMBLIES",      // Trusted assemblies (like the GAC)
        "APP_PATHS",                        // Directories to probe for application assemblies
        // "APP_NI_PATHS",                     // Directories to probe for application native images (not used in this sample)
        // "NATIVE_DLL_SEARCH_DIRECTORIES",    // Directories to probe for native dlls (not used in this sample)
    };

    const char* propertyValues[] = {
        tpaList.c_str(),
        runtimePath
    };

    // Start the CoreCLR runtime
    void* hostHandle;
    unsigned int domainId;

    int hr = initializeCoreClr(
                    runtimePath,        // AppDomain base path
                    "SampleHost",       // AppDomain friendly name
                    sizeof(propertyKeys) / sizeof(char*),   // Property count
                    propertyKeys,       // Property names
                    propertyValues,     // Property values
                    &hostHandle,        // Host handle
                    &domainId);         // AppDomain ID

    if (hr >= 0)
    {
        printf("CoreCLR started; AppDomain %d created\n", domainId);
    }
    else
    {
        printf("coreclr_initialize failed - status: 0x%08x\n", hr);
        return -1;
    }                  


    // Create delegate to managed code
    doWork_ptr managedDelegate;    
    hr = createManagedDelegate(
            hostHandle, 
            domainId,
            "ManagedLibrary",
            "ManagedLibrary.ManagedWorker",
            "DoWork",
            (void**)&managedDelegate);

    if (hr >= 0)
    {
        printf("Managed delegate created\n");
    }
    else
    {
        printf("coreclr_create_delegate failed - status: 0x%08x\n", hr);
        return -1;
    }    

    // Invoking managed delegate
    double data[4];
    data[0] = 0; 
    data[1] = 0.25;
    data[2] = 0.5;
    data[3] = 0.75;
    const char* ret = managedDelegate("Test job", 3, sizeof(data) / sizeof(double), data, ReportProgressCallback);

    printf("Managed code returned: %s\n", ret);

    // Shutdown CoreCLR
    hr = shutdownCoreClr(hostHandle, domainId);

    if (hr >= 0)
    {
        printf("CoreCLR successfully shutdown\n");
    }
    else
    {
        printf("coreclr_shutdown failed - status: 0x%08x\n", hr);
    }

#if WINDOWS
    if (!FreeLibrary(coreClr))
    {
        printf("Failed to free coreclr.dll\n");
    }
#elif LINUX
    if(dlclose(coreClr))
    {
        printf("Failed to free libcoreclr.so\n");
    }
#endif
    return 0;
}

#if WINDOWS
// Win32 directory search for .dll files
void BuildTpaList(const char* directory, const char* extension, std::string& tpaList)
{
    std::string searchPath(directory);
    searchPath.append(FS_SEPERATOR);
    searchPath.append("*");
    searchPath.append(extension);

    WIN32_FIND_DATAA findData;
    HANDLE fileHandle = FindFirstFileA(searchPath.c_str(), &findData);

    if (fileHandle != INVALID_HANDLE_VALUE)
    {
        do
        {
            // Append the assembly to the list
            tpaList.append(directory);
            tpaList.append(FS_SEPERATOR);
            tpaList.append(findData.cFileName);
            tpaList.append(PATH_DELIMITER);

            // Note that the CLR does not guarantee which assembly will be loaded if an assembly
            // is in the TPA list multiple times (perhaps from different paths or perhaps with different NI/NI.dll
            // extensions. Therefore, a real host should probably add items to the list in priority order and only
            // add a file if it's not already present on the list.
            //
            // For this simple sample, though, and because we're only loading TPA assemblies from a single path,
            // and have no native images, we can ignore that complication.
        }
        while (FindNextFileA(fileHandle, &findData));
        FindClose(fileHandle);
    }
}
#elif LINUX
// POSIX directory search for .dll files
void BuildTpaList(const char* directory, const char* extension, std::string& tpaList)
{
    DIR* dir = opendir(directory);
    struct dirent* entry;
    int extLength = strlen(extension);

    while ((entry = readdir(dir)) != NULL)
    {
        // This simple sample doesn't check for symlinks
        std::string filename(entry->d_name);

        // Check if the file has the right extension
        int extPos = filename.length() - extLength;
        if (extPos <= 0 || filename.compare(extPos, extLength, extension) != 0)
        {
            continue;
        }

        // Append the assembly to the list
        tpaList.append(directory);
        tpaList.append(FS_SEPERATOR);
        tpaList.append(filename);
        tpaList.append(PATH_DELIMITER);

        // Note that the CLR does not guarantee which assembly will be loaded if an assembly
        // is in the TPA list multiple times (perhaps from different paths or perhaps with different NI/NI.dll
        // extensions. Therefore, a real host should probably add items to the list in priority order and only
        // add a file if it's not already present on the list.
        //
        // For this simple sample, though, and because we're only loading TPA assemblies from a single path,
        // and have no native images, we can ignore that complication.
    }
}
#endif

// Callback function passed to managed code to facilitate calling back into native code with status
int ReportProgressCallback(int progress)
{
    printf("Received status from managed code: %d\n", progress);
    return -progress;
}