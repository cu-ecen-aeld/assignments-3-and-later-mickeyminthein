#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include <errno.h>
#include <syslog.h>

int main(int argc, char *argv[])
{
    /* Open syslog using LOG_USER facility */
    openlog("writer", LOG_PID, LOG_USER);

    /* Check arguments */
    if (argc != 3) {
        syslog(LOG_DEBUG, "Invalid arguments. Usage: writer <writefile> <writestr>");
        closelog();
        return 1;
    }

    const char *writefile = argv[1];
    const char *writestr  = argv[2];

    /* dirname() may modify its argument, so make a copy */
    char *path_copy = strdup(writefile);
    if (!path_copy) {
        syslog(LOG_ERR, "strdup failed");
        closelog();
        return 1;
    }

    char *dirpath = dirname(path_copy);

    struct stat st;
    if (stat(dirpath, &st) != 0 || !S_ISDIR(st.st_mode)) {
        syslog(LOG_ERR, "Directory does not exist: %s", dirpath);
        free(path_copy);
        closelog();
        return 1;
    }

    free(path_copy);

    /* Open file for writing (creates if it doesn't exist) */
    FILE *fp = fopen(writefile, "w");
    if (!fp) {
        syslog(LOG_ERR, "Failed to open file '%s': %s",
               writefile, strerror(errno));
        closelog();
        return 1;
    }

    /* Write string to file */
    if (fprintf(fp, "%s\n", writestr) < 0) {
        syslog(LOG_ERR, "Failed to write to file '%s'", writefile);
        fclose(fp);
        closelog();
        return 1;
    }

    fclose(fp);

    return 0;
}

