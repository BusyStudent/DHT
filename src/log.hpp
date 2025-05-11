#pragma once

#include <format>
#include <cstdio>

#if defined(NDEBUG)
#define DO_LOG(mod, fmt, ...) do {} while(0)
#elif defined(__cpp_lib_print)
#define DO_LOG(mod, fmt, ...) std::println(stderr, mod " " fmt, ##__VA_ARGS__)
#include <print>
#else
#define DO_LOG(mod, fmt, ...) ::fprintf(stderr, mod " %s\n", std::format(fmt, ##__VA_ARGS__).c_str())
#endif

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

#ifndef GET_PEERS_LOG
#define GET_PEERS_LOG(fmt, ...) DO_LOG("[GetPeersManager]", fmt, ##__VA_ARGS__)
#endif

#ifndef SAMPLE_LOG
#define SAMPLE_LOG(fmt, ...) DO_LOG("[SampleManager]", fmt, ##__VA_ARGS__)
#endif