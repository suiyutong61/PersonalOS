#pragma once

#include <QObject>
#include <QString>

#include "models/Goal.h"
#include "models/Task.h"

namespace PersonOS {

// QML 包装对象：把领域对象转换为 QML 可直接绑定的属性（3.3.6 桥接约定）
class TaskItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 id MEMBER m_id CONSTANT)
    Q_PROPERTY(QString title MEMBER m_title CONSTANT)
    Q_PROPERTY(QString status MEMBER m_status CONSTANT)
    Q_PROPERTY(QString statusText MEMBER m_statusText CONSTANT)
    Q_PROPERTY(QString planText MEMBER m_planText CONSTANT)
    Q_PROPERTY(bool done MEMBER m_done CONSTANT)
public:
    explicit TaskItem(const Task &t, QObject *parent = nullptr);

    qint64 m_id = 0;
    QString m_title;
    QString m_status;
    QString m_statusText;
    QString m_planText;
    bool m_done = false;
};

class GoalItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 id MEMBER m_id CONSTANT)
    Q_PROPERTY(QString title MEMBER m_title CONSTANT)
    Q_PROPERTY(QString levelText MEMBER m_levelText CONSTANT)
    Q_PROPERTY(int depth MEMBER m_depth CONSTANT)
    Q_PROPERTY(int priority MEMBER m_priority CONSTANT)
public:
    explicit GoalItem(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    qint64 m_id = 0;
    QString m_title;
    QString m_levelText;
    int m_depth = 0;
    int m_priority = 0;
};

} // namespace PersonOS
