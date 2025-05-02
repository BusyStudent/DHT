#pragma once

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QCryptographicHash> // For hashing
#include <QDebug>             // For output
#include <QListWidget>        // Include QListWidget
#include <QListWidgetItem>    // Include QListWidgetItem
#include <QMenu>
#include <QPoint>
#include <QWidget>

class LogListWidget : public QListWidget {
Q_OBJECT // Essential for signals and slots

    public : explicit LogListWidget(QWidget *parent = nullptr);
    ~LogListWidget();

private slots:
    // Slot to handle the context menu request
    void showCustomContextMenu(const QPoint &pos);

    // Slots for handling the custom copy actions
    void onCopyAsText(QString text);
    void onCopyAsFile(QStringList files);

private:
    // Helper function for demonstration purposes
    //   QString generateMagnetLink(const QString &data);
};