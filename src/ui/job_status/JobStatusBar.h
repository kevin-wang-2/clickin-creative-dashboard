#pragma once
#include <QWidget>

class JobStatusBar : public QWidget {
    Q_OBJECT
public:
    explicit JobStatusBar(QWidget* parent = nullptr);
};
