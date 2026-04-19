#ifndef LOGGER_H
#define LOGGER_H

/**
 * @file logger.h
 * @brief Naplózó rendszer szintű szűréssel és rotációval.
 */

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} LogLevel;

/**
 * @brief Naplózó inicializálása.
 * @param log_file_path Naplófájl elérési útja (NULL = csak stderr).
 * @param level Minimális naplószint.
 * @return 0 siker, -1 hiba esetén.
 */
int logger_init(const char *log_file_path, LogLevel level);

/**
 * @brief Naplóbejegyzés írása.
 */
void logger_log(LogLevel level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * @brief Naplózó lezárása.
 */
void logger_close(void);

#define LOG_DEBUG_MSG(fmt, ...) logger_log(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO_MSG(fmt, ...)  logger_log(LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN_MSG(fmt, ...)  logger_log(LOG_WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR_MSG(fmt, ...) logger_log(LOG_ERROR, fmt, ##__VA_ARGS__)

#endif /* LOGGER_H */
