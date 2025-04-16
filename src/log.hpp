#pragma once

#include <format>
#include <cstdio>

#ifndef DHT_LOG
#define DHT_LOG(fmt, ...) ::fprintf(stderr, "[DHT] %s\n", std::format(fmt, ##__VA_ARGS__).c_str())
#endif