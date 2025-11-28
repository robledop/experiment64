#include <unistd.h>

int mkdir(const char *path, [[maybe_unused]] int mode)
{
    // No directory creation support yet; pretend success for simple ports.
    (void)path;
    (void)mode;
    return 0;
}
