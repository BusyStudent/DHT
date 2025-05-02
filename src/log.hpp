#pragma once

#include <format>
#include <cstdio>

#ifndef DHT_LOG
#define DHT_LOG(fmt, ...) ::fprintf(stderr, "[DHT] %s\n", std::format(fmt, ##__VA_ARGS__).c_str())
#endif

#ifndef BT_LOG
#define BT_LOG(fmt, ...) ::fprintf(stderr, "[BT] %s\n", std::format(fmt, ##__VA_ARGS__).c_str())
#endif

#ifndef UTP_LOG
#define UTP_LOG(fmt, ...) ::fprintf(stderr, "[UTP] %s\n", std::format(fmt, ##__VA_ARGS__).c_str())
#endif


#ifndef APP_LOG
#define APP_LOG(fmt, ...) ::fprintf(stderr, "[APP] %s\n", std::format(fmt, ##__VA_ARGS__).c_str())
#endif