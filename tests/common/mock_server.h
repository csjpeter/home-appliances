#ifndef MOCK_SERVER_H
#define MOCK_SERVER_H

/**
 * @file mock_server.h
 * @brief Shared infrastructure for pthread-based mock device servers.
 *
 * Usage pattern:
 *
 *   static volatile int mock_ready = 0;
 *
 *   static void *my_mock(void *arg) {
 *       int fd = socket(...);
 *       // setsockopt SO_REUSEADDR
 *       // bind to 127.0.0.1:PORT
 *       mock_ready = 1;           // signal to test thread
 *       // handle one exchange
 *       close(fd);
 *       return NULL;
 *   }
 *
 *   static void test_something(void) {
 *       mock_ready = 0;
 *       pthread_t tid;
 *       pthread_create(&tid, NULL, my_mock, NULL);
 *       MOCK_WAIT_READY(mock_ready);
 *       // call real client code
 *       pthread_join(tid, NULL);
 *       ASSERT(...);
 *   }
 */

#include <unistd.h>

/* Spin-wait until the mock signals it is ready (bound and listening). */
#define MOCK_WAIT_READY(flag) \
    do { while (!(flag)) usleep(500); } while (0)

/* Loopback address string used by all mocks and tests. */
#define MOCK_HOST "127.0.0.1"

/* Test ports — high enough to avoid root and production conflicts. */
#define MOCK_GREE_PORT          17000
#define MOCK_ROBOROCK_PORT      15432
#define MOCK_SAMSUNG_TV_PORT    15500
#define MOCK_BROTHER_SNMP_PORT  16100

#endif /* MOCK_SERVER_H */
