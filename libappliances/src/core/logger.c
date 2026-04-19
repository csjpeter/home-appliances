#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#define MAX_LOG_SIZE (5 * 1024 * 1024)
#define MAX_ROTATIONS 5

static FILE *g_log_file = NULL;
static LogLevel g_level = LOG_INFO;

static const char *level_str(LogLevel level)
{
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO";
        case LOG_WARN:  return "WARN";
        case LOG_ERROR: return "ERROR";
    }
    return "?";
}

int logger_init(const char *log_file_path, LogLevel level)
{
    g_level = level;
    if (!log_file_path)
        return 0;
    g_log_file = fopen(log_file_path, "a");
    if (!g_log_file) {
        fprintf(stderr, "logger: cannot open: %s\n", log_file_path);
        return -1;
    }
    return 0;
}

void logger_log(LogLevel level, const char *fmt, ...)
{
    if (level < g_level)
        return;

    char ts[32];
    time_t now = time(NULL);
    struct tm tm_info = {0};
    localtime_r(&now, &tm_info);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);

    FILE *out = (level >= LOG_WARN) ? stderr : stdout;
    fprintf(out, "[%s] [%s] ", ts, level_str(level));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fputc('\n', out);

    if (!g_log_file)
        return;

    fprintf(g_log_file, "[%s] [%s] ", ts, level_str(level));
    va_start(ap, fmt);
    vfprintf(g_log_file, fmt, ap);
    va_end(ap);
    fputc('\n', g_log_file);
    fflush(g_log_file);
}

void logger_close(void)
{
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}
