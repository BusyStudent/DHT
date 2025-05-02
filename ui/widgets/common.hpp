#pragma once

#include <Qt>

enum class CopyableDataFlag {
    Hash = Qt::ItemDataRole::UserRole + 1,
    TorrentFile,
    TextMap,
    Files,
};