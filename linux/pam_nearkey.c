#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// PAM provides these symbols when the module is loaded by GDM, SDDM, or LightDM.
typedef struct pam_handle pam_handle_t;
extern int pam_get_user(pam_handle_t*, const char**, const char*);
#define PAM_SUCCESS 0
#define PAM_IGNORE 25

int pam_sm_authenticate(pam_handle_t* handle, int flags, int argc, const char** argv) {
    (void)flags; (void)argc; (void)argv;
    const char* user = NULL;
    if (pam_get_user(handle, &user, NULL) != PAM_SUCCESS || !user || !*user) return PAM_IGNORE;
    const int fd = open("/run/nearkey/status", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return PAM_IGNORE;
    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != 0 ||
        (info.st_mode & 022) != 0 || info.st_size < 1 || info.st_size > 1024) {
        close(fd);
        return PAM_IGNORE;
    }
    char text[1025];
    const ssize_t length = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (length < 1) return PAM_IGNORE;
    text[length] = 0;
    char expected[256];
    if (strlen(user) > 200) return PAM_IGNORE;
    snprintf(expected, sizeof(expected), "user=%s\n", user);
    if (strncmp(text, expected, strlen(expected)) != 0) return PAM_IGNORE;
    const char* near = strstr(text, "\nnear=1\n");
    const char* updated = strstr(text, "\nupdated_unix=");
    if (!near || !updated) return PAM_IGNORE;
    char* end = NULL;
    errno = 0;
    const long long timestamp = strtoll(updated + strlen("\nupdated_unix="), &end, 10);
    const time_t now = time(NULL);
    if (errno || !end || *end != '\n' || timestamp > now || now - timestamp > 12) return PAM_IGNORE;
    return PAM_SUCCESS;
}

int pam_sm_setcred(pam_handle_t* handle, int flags, int argc, const char** argv) {
    (void)handle; (void)flags; (void)argc; (void)argv;
    return PAM_IGNORE;
}
