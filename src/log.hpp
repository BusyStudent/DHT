#pragma once

#include <format>
#include <cstdio>

#define DO_LOG(mod, ...) ::fprintf(stderr, mod " %s\n", std::format(##__VA_ARGS__).c_str())

#ifndef DHT_LOG
#define DHT_LOG(fmt, ...) DO_LOG("[DHT]", fmt, ##__VA_ARGS__)
#endif

#ifndef BT_LOG
#define BT_LOG(fmt, ...) DO_LOG("[BT]", fmt, ##__VA_ARGS__)
#endif

#ifndef UTP_LOG
#define UTP_LOG(fmt, ...) DO_LOG("[UTP]", fmt, ##__VA_ARGS__)
#endif


#ifndef APP_LOG
#define APP_LOG(fmt, ...) DO_LOG("[APP]", fmt, ##__VA_ARGS__)
#endif