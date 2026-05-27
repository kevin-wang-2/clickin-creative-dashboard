#pragma once
#include <QWidget>
#include <memory>

class JobStatusBar : public QWidget {
    Q_OBJECT
public:
    explicit JobStatusBar(QWidget* parent = nullptr);
    ~JobStatusBar();

public slots:
    void setStatus(const QString& text);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
